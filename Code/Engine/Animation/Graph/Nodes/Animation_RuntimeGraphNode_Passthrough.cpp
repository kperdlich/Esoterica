#include "Animation_RuntimeGraphNode_Passthrough.h"
#include "Engine/Animation/Graph/Animation_RuntimeGraph_Contexts.h"

//-------------------------------------------------------------------------

namespace EE::Animation::GraphNodes
{
    void PassthroughNode::Settings::InstantiateNode( TVector<GraphNode*> const& nodePtrs, GraphDataSet const* pDataSet, InstantiationOptions options ) const
    {
        EE_ASSERT( options == GraphNode::Settings::InstantiationOptions::NodeAlreadyCreated );
        auto pNode = static_cast<PassthroughNode*>( nodePtrs[m_nodeIdx] );
        SetNodePtrFromIndex( nodePtrs, m_childNodeIdx, pNode->m_pChildNode );
    }

    //-------------------------------------------------------------------------

    SyncTrack const& PassthroughNode::GetSyncTrack() const
    {
        if ( IsValid() )
        {
            return m_pChildNode->GetSyncTrack();
        }
        else
        {
            return SyncTrack::s_defaultTrack;
        }
    }

    void PassthroughNode::InitializeInternal( GraphContext& context, SyncTrackTime const& initialTime )
    {
        EE_ASSERT( context.IsValid() );
        EE_ASSERT( m_pChildNode != nullptr );
        PoseNode::InitializeInternal( context, initialTime );

        //-------------------------------------------------------------------------

        m_pChildNode->Initialize( context, initialTime );

        //-------------------------------------------------------------------------

        if ( m_pChildNode->IsValid() )
        {
            m_duration = m_pChildNode->GetDuration();
            m_previousTime = m_pChildNode->GetPreviousTime();
            m_currentTime = m_pChildNode->GetCurrentTime();
        }
        else
        {
            m_previousTime = m_currentTime = 0.0f;
            m_duration = 1.0f;
        }
    }

    void PassthroughNode::ShutdownInternal( GraphContext& context )
    {
        m_pChildNode->Shutdown( context );
        PoseNode::ShutdownInternal( context );
    }

    GraphPoseNodeResult PassthroughNode::Update( GraphContext& context )
    {
        EE_ASSERT( context.IsValid() );
        MarkNodeActive( context );

        GraphPoseNodeResult result;

        // Forward child node results
        if ( IsValid() )
        {
            result = m_pChildNode->Update( context );
            m_duration = m_pChildNode->GetDuration();
            m_previousTime = m_pChildNode->GetPreviousTime();
            m_currentTime = m_pChildNode->GetCurrentTime();
        }
        else
        {
            result.m_sampledEventRange = SampledEventRange( context.m_sampledEventsBuffer.GetNumEvents() );
        }

        return result;
    }

    GraphPoseNodeResult PassthroughNode::Update( GraphContext& context, SyncTrackTimeRange const& updateRange )
    {
        EE_ASSERT( context.IsValid() );
        MarkNodeActive( context );

        GraphPoseNodeResult result;

        // Forward child node results
        if ( IsValid() )
        {
            result = m_pChildNode->Update( context, updateRange );
            m_duration = m_pChildNode->GetDuration();
            m_previousTime = m_pChildNode->GetPreviousTime();
            m_currentTime = m_pChildNode->GetCurrentTime();
        }
        else
        {
            result.m_sampledEventRange = SampledEventRange( context.m_sampledEventsBuffer.GetNumEvents() );
        }

        return result;
    }

    void PassthroughNode::DeactivateBranch( GraphContext& context )
    {
        if ( IsValid() )
        {
            PoseNode::DeactivateBranch( context );
            m_pChildNode->DeactivateBranch( context );
        }
    }
}