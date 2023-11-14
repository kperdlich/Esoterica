#include "AIAction_Idle.h"
#include "Game/AI/Behaviors/AIBehavior.h"
#include "Game/AI/Animation/AIAnimationController.h"
#include "Base/Math/MathRandom.h"

//-------------------------------------------------------------------------

namespace EE::AI
{
    void IdleAction::Start( BehaviorContext const& ctx )
    {
        ctx.m_pAnimationController->SetCharacterState( CharacterAnimationState::Locomotion );
        ctx.m_pAnimationController->SetIdle();

        ResetIdleBreakerTimer();
    }

    void IdleAction::ResetIdleBreakerTimer()
    {
        m_idleBreakerCooldown.Start( Math::GetRandomFloat( 15.0f, 25.0f ) );
    }

    void IdleAction::Update( BehaviorContext const& ctx )
    {
        if ( m_idleBreakerCooldown.IsRunning() )
        {
            if ( m_idleBreakerCooldown.Update( ctx.GetDeltaTime() ) )
            {
                // TODO: run idle breaker
            }
        }
    }
}
