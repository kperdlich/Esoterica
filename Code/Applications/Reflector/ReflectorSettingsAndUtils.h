#pragma once

#include "System/Types/String.h"
#include "System/FileSystem/FileSystemPath.h"

//-------------------------------------------------------------------------

namespace EE::TypeSystem::Reflection
{
    enum class ReflectionMacro
    {
        IgnoreHeader,
        RegisterModule,
        RegisterEnum,
        RegisterType,
        RegisterResource,
        RegisterTypeResource,
        RegisterVirtualResource,
        RegisterEntityComponent,
        RegisterSingletonEntityComponent,
        RegisterEntitySystem,
        RegisterEntityWorldSystem,
        RegisterProperty,
        ExposeProperty,

        NumMacros,
        Unknown = NumMacros,
    };

    char const* GetReflectionMacroText( ReflectionMacro macro );

    //-------------------------------------------------------------------------

    namespace Settings
    {
        constexpr static char const* const g_engineNamespace = "EE";
        constexpr static char const* const g_engineNamespacePlusDelimiter = "EE::";
        constexpr static char const* const g_moduleHeaderParentDirectoryName = "_Module";
        constexpr static char const* const g_moduleHeaderSuffix = "Module.h";
        constexpr static char const* const g_autogeneratedDirectory = "_Module\\_AutoGenerated\\";
        constexpr static char const* const g_autogeneratedModuleFile = "_module.cpp";
        constexpr static char const* const g_globalAutoGeneratedDirectory = "Code\\Applications\\Shared\\_AutoGenerated\\";
        constexpr static char const* const g_engineTypeRegistrationHeaderPath = "EngineTypeRegistration.h";
        constexpr static char const* const g_toolsTypeRegistrationHeaderPath = "ToolsTypeRegistration.h";
        constexpr static char const* const g_temporaryDirectoryPath = "\\..\\_Temp\\";

        constexpr static char const* const g_devToolsExclusionDefine = "-D EE_SHIPPING";

        //-------------------------------------------------------------------------
        // Projects
        //-------------------------------------------------------------------------

        constexpr static char const* const g_moduleNameExclusionFilters[] = { "ThirdParty" };

        char const* const g_allowedProjectNames[] =
        {
            "Esoterica.System",
            "Esoterica.Engine.Runtime",
            "Esoterica.Game.Runtime",
            "Esoterica.Engine.Tools",
            "Esoterica.Game.Tools",
        };

        constexpr static int const g_numAllowedProjects = sizeof( g_allowedProjectNames ) / sizeof( g_allowedProjectNames[0] );

        //-------------------------------------------------------------------------
        // Core class names
        //-------------------------------------------------------------------------

        constexpr static char const* const g_registeredTypeInterfaceClassName = "IRegisteredType";
        constexpr static char const* const g_baseEntityClassName = "Entity";
        constexpr static char const* const g_baseEntityComponentClassName = "EntityComponent";
        constexpr static char const* const g_baseEntitySystemClassName = "IEntitySystem";
        constexpr static char const* const g_baseResourceClassName = "Resource::IResource";

        //-------------------------------------------------------------------------
        // Clang Parser Settings
        //-------------------------------------------------------------------------

        char const* const g_includePaths[] =
        {
            "Code\\",
            "Code\\System\\ThirdParty\\EA\\EABase\\include\\common\\",
            "Code\\System\\ThirdParty\\EA\\EASTL\\include\\",
            "Code\\System\\ThirdParty\\",
            "Code\\System\\ThirdParty\\imgui\\",
            "External\\PhysX\\pxshared\\include\\",
            "External\\PhysX\\physx\\include\\",
            #if EE_ENABLE_NAVPOWER
            "External\\NavPower\\include\\"
            #endif
        };
    }

    //-------------------------------------------------------------------------

    namespace Utils
    {
        inline bool IsFileUnderToolsProject( FileSystem::Path const& filePath )
        {
            auto const& filePathStr = filePath.GetString();

            return filePathStr.find( Settings::g_allowedProjectNames[3] ) != String::npos || filePath.GetString().find( Settings::g_allowedProjectNames[4] ) != String::npos;
        }
    }
}