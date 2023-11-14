#include "ResourceServer.h"
#include "_AutoGenerated/ToolsTypeRegistration.h"
#include "EngineTools/Resource/ResourceCompiler.h"
#include "EngineTools/ThirdParty/subprocess/subprocess.h"
#include "Engine/Entity/EntityDescriptors.h"
#include "Engine/Entity/EntitySerialization.h"
#include "Base/Resource/ResourceProviders/ResourceNetworkMessages.h"
#include "Base/IniFile.h"
#include "Base/FileSystem/FileSystem.h"
#include "Base/FileSystem/FileSystemUtils.h"

//-------------------------------------------------------------------------

namespace EE::Resource
{
    class CompilationTask final : public ITaskSet
    {

    public:

        CompilationTask( ResourceServerContext const& context, CompilationRequest* pRequest )
            : ITaskSet( 1 )
            , m_context( context )
            , m_pRequest( pRequest )
        {
            EE_ASSERT( m_context.IsValid() );

            // No default ctor for subprocess struct, so zero-init
            Memory::MemsetZero( &m_subProcess );
        }

        ~CompilationTask()
        {
            EE_ASSERT( !subprocess_alive( &m_subProcess ) );
        }

        inline CompilationRequest* GetRequest() const { return m_pRequest; }

    private:

        virtual void ExecuteRange( TaskSetPartition range, uint32_t threadnum ) override final
        {
            // If we are not exiting the application and the request needs to be processed
            // Note: we enqueue failed requests as well just to have a uniform code flow
            if ( !m_context.m_isExiting && !m_pRequest->IsComplete() )
            {
                EE_ASSERT( !m_pRequest->m_compilerArgs.empty() );
                char const* processCommandLineArgs[6] = { m_context.m_compilerExecutablePath.c_str(), "-compile", m_pRequest->m_compilerArgs.c_str(), nullptr, nullptr, nullptr };

                // Set force compilation flag
                if ( m_pRequest->RequiresForcedRecompiliation() )
                {
                    processCommandLineArgs[3] = "-force";
                }

                // Set package flag for packing request
                if ( m_pRequest->m_origin == CompilationRequest::Origin::Package )
                {
                    processCommandLineArgs[3] = "-package";
                }

                // Start compiler process
                //-------------------------------------------------------------------------

                m_pRequest->m_status = CompilationRequest::Status::Compiling;
                m_pRequest->m_compilationTimeStarted = PlatformClock::GetTime();

                int32_t result = subprocess_create( processCommandLineArgs, subprocess_option_combined_stdout_stderr | subprocess_option_inherit_environment | subprocess_option_no_window, &m_subProcess );
                if ( result != 0 )
                {
                    m_pRequest->m_status = CompilationRequest::Status::Failed;
                    m_pRequest->m_log = "Resource compiler failed to start!";
                    m_pRequest->m_compilationTimeFinished = PlatformClock::GetTime();
                    return;
                }

                // Wait for compilation to complete
                //-------------------------------------------------------------------------

                int32_t exitCode;
                result = subprocess_join( &m_subProcess, &exitCode );
                if ( result != 0 )
                {
                    m_pRequest->m_status = CompilationRequest::Status::Failed;
                    m_pRequest->m_log = "Resource compiler failed to complete!";
                    m_pRequest->m_compilationTimeFinished = PlatformClock::GetTime();
                    subprocess_destroy( &m_subProcess );
                    return;
                }

                // Handle completed compilation
                //-------------------------------------------------------------------------

                m_pRequest->m_compilationTimeFinished = PlatformClock::GetTime();

                CompilationResult const compilationResult = (CompilationResult) exitCode;

                switch ( compilationResult )
                {
                    case CompilationResult::SuccessUpToDate:
                    {
                        m_pRequest->m_status = CompilationRequest::Status::SucceededUpToDate;
                    }
                    break;

                    case CompilationResult::Success:
                    {
                        m_pRequest->m_status = CompilationRequest::Status::Succeeded;
                    }
                    break;

                    case CompilationResult::SuccessWithWarnings:
                    {
                        m_pRequest->m_status = CompilationRequest::Status::SucceededWithWarnings;
                    }
                    break;

                    default:
                    {
                        m_pRequest->m_status = CompilationRequest::Status::Failed;
                    }
                    break;
                }

                // Read error and output of process
                //-------------------------------------------------------------------------

                char readBuffer[512];
                while ( fgets( readBuffer, 512, subprocess_stdout( &m_subProcess ) ) )
                {
                    m_pRequest->m_log += readBuffer;
                }

                // Strip the process preamble and delimiter
                size_t const delimiterPos = m_pRequest->m_log.find_first_of( CompilationLog::s_delimiter );
                if ( delimiterPos != String::npos )
                {
                    size_t const delimiterLength = strlen( CompilationLog::s_delimiter );
                    m_pRequest->m_log = m_pRequest->m_log.substr( delimiterLength + 1, m_pRequest->m_log.length() - delimiterLength - 1 );
                }

                //-------------------------------------------------------------------------

                subprocess_destroy( &m_subProcess );
            }
        }

    private:

        ResourceServerContext const&                        m_context;
        CompilationRequest*                                 m_pRequest = nullptr;
        subprocess_s                                        m_subProcess;
    };

    //-------------------------------------------------------------------------

    class PackagingTask final : public ITaskSet
    {
    public:

        PackagingTask( ResourceServerContext const& context, TVector<ResourceID> const& mapsToBePackaged )
            : ITaskSet( 1 )
            , m_context( context )
            , m_mapsToBePackaged( mapsToBePackaged )
        {
            EE_ASSERT( m_context.IsValid() );
        }

        inline TVector<ResourceID> const& GetRuntimeDependencies() const { return m_runtimeDependencies; }

    private:

        virtual void ExecuteRange( TaskSetPartition range, uint32_t threadnum ) override final
        {
            EngineModule::GetListOfAllRequiredModuleResources( m_runtimeDependencies );
            GameModule::GetListOfAllRequiredModuleResources( m_runtimeDependencies );

            //-------------------------------------------------------------------------

            for ( auto const& mapID : m_mapsToBePackaged )
            {
                EnqueueResourceForPackaging( mapID );
            }
        }

        void EnqueueResourceForPackaging( ResourceID const& resourceID )
        {
            if ( m_context.m_isExiting )
            {
                return;
            }

            //-------------------------------------------------------------------------

            auto pCompiler = m_context.m_pCompilerRegistry->GetCompilerForResourceType( resourceID.GetResourceTypeID() );
            if ( pCompiler != nullptr )
            {
                // Add resource for packaging
                VectorEmplaceBackUnique( m_runtimeDependencies, resourceID );

                // Get all runtime install dependencies
                TVector<ResourceID> referencedResources;
                pCompiler->GetInstallDependencies( resourceID, referencedResources );

                // Recursively enqueue all referenced resources
                for ( auto const& referenceResourceID : referencedResources )
                {
                    EnqueueResourceForPackaging( referenceResourceID );
                }
            }
        }

    public:

        ResourceServerContext const&            m_context;
        TVector<ResourceID> const&              m_mapsToBePackaged;
        TVector<ResourceID>                     m_runtimeDependencies;
    };

    //-------------------------------------------------------------------------

    ResourceServer::~ResourceServer()
    {
        EE_ASSERT( m_pCompilerRegistry == nullptr );
    }

    bool ResourceServer::Initialize( IniFile const& iniFile )
    {
        EE_ASSERT( iniFile.IsValid() );

        if ( !m_settings.ReadSettings( iniFile ) )
        {
            return false;
        }

        // Register types
        //-------------------------------------------------------------------------

        AutoGenerated::Tools::RegisterTypes( m_typeRegistry );

        m_pCompilerRegistry = EE::New<CompilerRegistry>( m_typeRegistry, m_settings.m_rawResourcePath );

        // Open network connection
        //-------------------------------------------------------------------------

        if ( !Network::NetworkSystem::Initialize() )
        {
            return false;
        }

        if ( !Network::NetworkSystem::StartServerConnection( &m_networkServer, m_settings.m_resourceServerPort ) )
        {
            return false;
        }

        // File System
        //-------------------------------------------------------------------------

        m_settings.m_rawResourcePath.EnsureDirectoryExists();
        m_settings.m_compiledResourcePath.EnsureDirectoryExists();
        m_fileSystemWatcher.StartWatching( m_settings.m_rawResourcePath );

        // Create Workers
        //-------------------------------------------------------------------------

        m_taskSystem.Initialize();

        m_context.m_rawResourcePath = m_settings.m_rawResourcePath;
        m_context.m_compiledResourcePath = m_settings.m_compiledResourcePath;
        m_context.m_compilerExecutablePath = m_settings.m_resourceCompilerExecutablePath;
        m_context.m_pTypeRegistry = &m_typeRegistry;
        m_context.m_pCompilerRegistry = m_pCompilerRegistry;

        // Packaging
        //-------------------------------------------------------------------------

        RefreshAvailableMapList();

        return true;
    }

    void ResourceServer::Shutdown()
    {
        m_context.m_isExiting = true;

        // Complete all scheduled requests
        //-------------------------------------------------------------------------

        m_taskSystem.WaitForAll();
        ProcessCompletedRequests();
        m_taskSystem.Shutdown();

        EE_ASSERT( m_numScheduledTasks == 0 );

        // Packaging
        //-------------------------------------------------------------------------

        if ( m_pPackagingTask != nullptr )
        {
            EE_ASSERT( m_pPackagingTask->GetIsComplete() );
            EE::Delete( m_pPackagingTask );
        }

        // Unregister File Watcher
        //-------------------------------------------------------------------------

        if ( m_fileSystemWatcher.IsWatching() )
        {
            m_fileSystemWatcher.StopWatching();
        }

        // Delete requests
        //-------------------------------------------------------------------------

        for ( auto& pRequest : m_requests )
        {
            EE::Delete( pRequest );
        }

        //-------------------------------------------------------------------------

        Network::NetworkSystem::StopServerConnection( &m_networkServer );
        Network::NetworkSystem::Shutdown();

        //-------------------------------------------------------------------------

        EE::Delete( m_pCompilerRegistry );

        AutoGenerated::Tools::UnregisterTypes( m_typeRegistry );
    }

    //-------------------------------------------------------------------------

    void ResourceServer::Update()
    {
        // Update network server
        //-------------------------------------------------------------------------

        Network::NetworkSystem::Update();

        if ( m_networkServer.IsRunning() )
        {
            auto ProcessIncomingMessages = [this] ( Network::IPC::Message const& message )
            {
                if ( message.GetMessageID() == (int32_t) NetworkMessageID::RequestResource )
                {
                    uint32_t const clientID = message.GetClientConnectionID();
                    NetworkResourceRequest networkRequest = message.GetData<NetworkResourceRequest>();
                    for ( auto const& resourceID : networkRequest.m_resourceIDs )
                    {
                        CreateResourceRequest( resourceID, clientID );
                    }
                }
            };

            m_networkServer.ProcessIncomingMessages( ProcessIncomingMessages );
        }

        // Update Packaging
        //-------------------------------------------------------------------------

        if ( m_packagingStage == PackagingStage::Preparing )
        {
            EE_ASSERT( m_pPackagingTask != nullptr );

            if ( m_pPackagingTask->GetIsComplete() )
            {
                for ( auto const& resourceID : m_pPackagingTask->GetRuntimeDependencies() )
                {
                    m_packagingRequests.emplace_back( CreateResourceRequest( resourceID, 0, CompilationRequest::Origin::Package ) );
                }

                EE::Delete( m_pPackagingTask );
                m_packagingStage = PackagingStage::Packaging;
            }
        }
        else if ( m_packagingStage == PackagingStage::Packaging )
        {
            bool isComplete = true;

            for ( auto pRequest : m_packagingRequests )
            {
                if ( !pRequest->IsComplete() )
                {
                    isComplete = false;
                    break;
                }
            }

            if ( isComplete )
            {
                m_packagingRequests.clear();
                m_packagingStage = PackagingStage::Complete;
            }
        }

        // Process completed requests
        //-------------------------------------------------------------------------
        
        ProcessCompletedRequests();

        // Process cleanup request
        //-------------------------------------------------------------------------

        if ( m_cleanupRequested )
        {
            for ( int32_t i = int32_t( m_requests.size() ) - 1; i >= 0; i-- )
            {
                if ( m_requests[i]->IsComplete() )
                {
                    EE::Delete( m_requests[i] );
                    m_requests.erase( m_requests.begin() + i );
                }
            }

            m_cleanupRequested = false;
        }

        // Update File System Watcher
        //-------------------------------------------------------------------------

        if ( m_fileSystemWatcher.IsWatching() )
        {
            if ( m_fileSystemWatcher.Update() )
            {
                for ( FileSystem::Watcher::Event const& fsEvent : m_fileSystemWatcher.GetFileSystemChangeEvents() )
                {
                    if ( fsEvent.m_type != FileSystem::Watcher::Event::FileModified )
                    {
                        continue;
                    }

                    //-------------------------------------------------------------------------

                    EE_ASSERT( fsEvent.m_path.IsValid() && fsEvent.m_path.IsFilePath() );

                    ResourcePath resourcePath = ResourcePath::FromFileSystemPath( m_settings.m_rawResourcePath, fsEvent.m_path );
                    if ( !resourcePath.IsValid() )
                    {
                        return;
                    }

                    ResourceID resourceID( resourcePath );
                    if ( !resourceID.IsValid() )
                    {
                        return;
                    }

                    // If we have a record, then schedule a recompile task
                    CreateResourceRequest( resourceID, 0, CompilationRequest::Origin::FileWatcher );
                }
            }
        }
    }

    bool ResourceServer::IsBusy() const
    {
        return IsPackaging() || m_numScheduledTasks != 0;
    }

    //-------------------------------------------------------------------------

    CompilationRequest* ResourceServer::CreateResourceRequest( ResourceID const& resourceID, uint32_t clientID, CompilationRequest::Origin origin )
    {
        CompilationRequest* pRequest = EE::New<CompilationRequest>();

        if ( resourceID.IsValid() )
        {
            if ( origin == CompilationRequest::Origin::External )
            {
                EE_ASSERT( clientID != 0 );
            }
            else
            {
                EE_ASSERT( clientID == 0 );
            }

            //-------------------------------------------------------------------------

            pRequest->m_clientID = clientID;
            pRequest->m_origin = origin;
            pRequest->m_resourceID = resourceID;
            pRequest->m_sourceFile = ResourcePath::ToFileSystemPath( m_settings.m_rawResourcePath, pRequest->m_resourceID.GetResourcePath() );
            pRequest->m_compilerArgs = pRequest->m_resourceID.GetResourcePath().c_str();
            pRequest->m_status = CompilationRequest::Status::Pending;

            // Set the destination path based on request type
            if ( origin == CompilationRequest::Origin::Package )
            {
                pRequest->m_destinationFile = ResourcePath::ToFileSystemPath( m_settings.m_packagedBuildCompiledResourcePath, pRequest->m_resourceID.GetResourcePath() );
            }
            else
            {
                pRequest->m_destinationFile = ResourcePath::ToFileSystemPath( m_settings.m_compiledResourcePath, pRequest->m_resourceID.GetResourcePath() );
            }
        }
        else // Invalid resource ID
        {
            pRequest->m_log.sprintf( "Error: Invalid resource ID ( %s )", resourceID.c_str() );
            pRequest->m_status = CompilationRequest::Status::Failed;
        }

        // Enqueue new request
        //-------------------------------------------------------------------------

        m_requests.emplace_back( pRequest );
        auto pTask = EE::New<CompilationTask>( m_context, pRequest );
        m_taskSystem.ScheduleTask( pTask );
        m_activeTasks.emplace_back( pTask );
        m_numScheduledTasks++;

        //-------------------------------------------------------------------------

        return pRequest;
    }

    void ResourceServer::ProcessCompletedRequests()
    {
        // Create a bucket per connected client
        //-------------------------------------------------------------------------

        struct Bucket
        {
            void AddUpdateResponse( ResourceID const& ID, String const& filePath, String const& log = String() )
            {
                if ( m_updateResponses.empty() )
                {
                    m_updateResponses.push_back();
                }

                m_updateResponses.back().m_results.emplace_back( ID, filePath, log );

                if ( m_updateResponses.size() == 64 )
                {
                    m_updateResponses.push_back();
                }
            }

            void AddRequestResponse( ResourceID const& ID, String const& filePath, String const& log = String() )
            {
                if ( m_requestResponses.empty() )
                {
                    m_requestResponses.push_back();
                }

                m_requestResponses.back().m_results.emplace_back( ID, filePath, log );

                if ( m_requestResponses.size() == 64 )
                {
                    m_requestResponses.push_back();
                }
            }

            TVector<NetworkResourceResponse> m_updateResponses;
            TVector<NetworkResourceResponse> m_requestResponses;
        };

        TInlineVector<Bucket, 20> clientBuckets;

        auto const& connectedClients = m_networkServer.GetConnectedClients();
        int32_t const numConnectedClients = m_networkServer.GetNumConnectedClients();
        clientBuckets.resize( numConnectedClients );

        // Fill buckets
        //-------------------------------------------------------------------------

        for ( int32_t i = (int32_t) m_activeTasks.size() - 1; i >= 0; i-- )
        {
            CompilationTask* pActiveTask = m_activeTasks[i];

            if ( pActiveTask->GetIsComplete() )
            {
                auto pRequest = pActiveTask->GetRequest();
                EE_ASSERT( pRequest->IsComplete() );

                // No notifications if exiting
                if ( !m_context.m_isExiting )
                {
                    // Notify all clients
                    if ( pRequest->IsInternalRequest() )
                    {
                        // No need to notify the client for internal requests resources that are up to date
                        if ( pRequest->m_status != CompilationRequest::Status::SucceededUpToDate )
                        {
                            // Bulk notify all connected client that a resource has been recompiled so that they can reload it if necessary
                            for ( auto& clientBucket : clientBuckets )
                            {
                                if ( pRequest->HasSucceeded() )
                                {
                                    clientBucket.AddUpdateResponse( pRequest->GetResourceID(), pRequest->GetDestinationFilePath().ToString() );
                                }
                                else
                                {
                                    clientBucket.AddUpdateResponse( pRequest->GetResourceID(), "", pRequest->GetLog() );
                                }
                            }
                        }
                    }
                    else // Notify single client
                    {
                        for ( int32_t clientIdx = 0; clientIdx < numConnectedClients; clientIdx++ )
                        {
                            if ( connectedClients[clientIdx].m_ID == pRequest->GetClientID() )
                            {
                                if ( pRequest->HasSucceeded() )
                                {
                                    clientBuckets[clientIdx].AddRequestResponse( pRequest->GetResourceID(), pRequest->GetDestinationFilePath().ToString() );
                                }
                                else
                                {
                                    clientBuckets[clientIdx].AddRequestResponse( pRequest->GetResourceID(), "", pRequest->GetLog() );
                                }
                            }
                        }
                    }
                }

                // Delete task
                EE::Delete( pActiveTask );
                m_activeTasks.erase_unsorted( m_activeTasks.begin() + i );

                // Decrement task counter
                m_numScheduledTasks--;
            }
        }

        // Send Messages
        //-------------------------------------------------------------------------

        // Send messages per bucket
        for ( auto i = 0; i < numConnectedClients; i++ )
        {
            // Update notifications
            for( auto const& response : clientBuckets[i].m_updateResponses )
            {
                Network::IPC::Message message;
                message.SetClientConnectionID( connectedClients[i].m_ID );
                message.SetData( (int32_t) NetworkMessageID::ResourceUpdated, response );
                m_networkServer.SendNetworkMessage( eastl::move( message ) );
            }

            // Completed requests
            for ( auto const& response : clientBuckets[i].m_requestResponses )
            {
                Network::IPC::Message message;
                message.SetClientConnectionID( connectedClients[i].m_ID );
                message.SetData( (int32_t) NetworkMessageID::ResourceRequestComplete, response );
                m_networkServer.SendNetworkMessage( eastl::move( message ) );
            }
        }
    }

    //-------------------------------------------------------------------------

    void ResourceServer::RefreshAvailableMapList()
    {
        m_allMaps.clear();

        TVector<FileSystem::Path> results;
        if ( FileSystem::GetDirectoryContents( m_settings.m_rawResourcePath, results, FileSystem::DirectoryReaderOutput::OnlyFiles, FileSystem::DirectoryReaderMode::Expand, { "map" } ) )
        {
            for ( auto const& foundMapPath : results )
            {
                m_allMaps.emplace_back( ResourceID::FromFileSystemPath( m_settings.m_rawResourcePath, foundMapPath ) );
            }
        }
    }

    void ResourceServer::AddMapToPackagingList( ResourceID mapResourceID )
    {
        EE_ASSERT( mapResourceID.GetResourceTypeID() == EntityModel::SerializedEntityMap::GetStaticResourceTypeID() );
        VectorEmplaceBackUnique( m_mapsToBePackaged, mapResourceID );
    }

    void ResourceServer::RemoveMapFromPackagingList( ResourceID mapResourceID )
    {
        EE_ASSERT( mapResourceID.GetResourceTypeID() == EntityModel::SerializedEntityMap::GetStaticResourceTypeID() );
        m_mapsToBePackaged.erase_first_unsorted( mapResourceID );
    }

    bool ResourceServer::CanStartPackaging() const
    {
        return ( m_packagingStage == PackagingStage::None || m_packagingStage == PackagingStage::Complete ) && !m_mapsToBePackaged.empty();
    }

    void ResourceServer::StartPackaging()
    {
        EE_ASSERT( CanStartPackaging() );

        m_pPackagingTask = EE::New<PackagingTask>( m_context, m_mapsToBePackaged );
        m_taskSystem.ScheduleTask( m_pPackagingTask );
        m_packagingStage = PackagingStage::Preparing;
    }

    float ResourceServer::GetPackagingProgress() const
    {
        switch ( m_packagingStage )
        {
            case PackagingStage::None:
            {
                return 1.0f;
            }
            break;

            case PackagingStage::Preparing:
            {
                return 0.1f;
            }
            break;

            case PackagingStage::Packaging:
            {
                float numComplete = 0.0f;
                for ( auto pRequest : m_packagingRequests )
                {
                    if ( pRequest->IsComplete() )
                    {
                        numComplete++;
                    }
                }

                float const percentageComplete = numComplete / m_packagingRequests.size();
                return 0.05f + ( 0.95f * percentageComplete );
            }
            break;

            case PackagingStage::Complete:
            {
                return 1.0f;
            }
            break;
        }

        return 0.0f;
    }
}