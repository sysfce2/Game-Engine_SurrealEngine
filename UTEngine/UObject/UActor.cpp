
#include "Precomp.h"
#include "UActor.h"
#include "UMesh.h"
#include "VM/ScriptCall.h"

static std::string tickEventName = "Tick";

void UActor::Tick(float elapsed, bool tickedFlag)
{
	bTicked() = tickedFlag;

	TickAnimation(elapsed);

	if (Role() >= ROLE_SimulatedProxy && IsEventEnabled(tickEventName))
	{
		CallEvent(this, tickEventName, { ExpressionValue::FloatValue(elapsed) });
	}

	if (Role() == ROLE_Authority && RemoteRole() == ROLE_AutonomousProxy)
	{
		TimerCounter() += elapsed;
		while (TimerRate() > 0.0f && TimerCounter() > TimerRate())
		{
			TimerCounter() -= TimerRate();
			if (bTimerLoop())
				TimerRate() = 0.0f;
			CallEvent(this, "Timer");
		}
	}
}

bool UActor::HasAnim(const std::string& sequence)
{
	return Mesh() && Mesh()->GetSequence(sequence);
}

bool UActor::IsAnimating()
{
	return AnimRate() != 0.0f;
}

std::string UActor::GetAnimGroup(const std::string& sequence)
{
	if (Mesh())
	{
		MeshAnimSeq* seq = Mesh()->GetSequence(sequence);
		if (seq)
			return seq->Group;
	}
	return {};
}

void UActor::PlayAnim(const std::string& sequence, float* rate, float* tweenTime)
{
	if (Mesh())
	{
		MeshAnimSeq* seq = Mesh()->GetSequence(sequence);
		if (seq)
		{
			AnimFrame() = 0.0f;
			AnimRate() = 1.0f / seq->NumFrames * (seq->Rate * (rate ? *rate : 1.0f));
			bAnimLoop() = false;
		}
	}
}

void UActor::LoopAnim(const std::string& sequence, float* rate, float* tweenTime, float* minRate)
{
	if (Mesh())
	{
		MeshAnimSeq* seq = Mesh()->GetSequence(sequence);
		if (seq)
		{
			AnimFrame() = 0.0f;
			AnimRate() = 1.0f / seq->NumFrames * (seq->Rate * (rate ? *rate : 1.0f));
			bAnimLoop() = true;
		}
	}
}

void UActor::TickAnimation(float elapsed)
{
	if (Mesh())
	{
		for (int i = 0; AnimRate() != 0.0f && elapsed > 0.0f && i < 10; i++)
		{
			float fromFrame = AnimFrame();
			float animRate = (AnimRate() >= 0) ? AnimRate() : std::max(AnimMinRate(), -AnimRate() * length(Velocity()));
			float toFrame = fromFrame + animRate * elapsed;

			if (toFrame > 1.0f)
			{
				elapsed -= (toFrame - fromFrame) / animRate;
				toFrame = 1.0f;
			}

			MeshAnimSeq* seq = Mesh()->GetSequence(AnimSequence());
			if (seq)
			{
				for (const MeshAnimNotify& n : seq->Notifys)
				{
					if (n.Time > fromFrame && n.Time <= toFrame)
					{
						if (FindEventFunction(this, n.Function))
						{
							AnimFrame() = n.Time;
							CallEvent(this, n.Function);
						}
					}
				}
			}

			if (toFrame == 1.0f)
			{
				// To do: bAnimFinished() and release any latent FinishAnim function

				AnimFrame() = 0.0f;
				if (!bAnimLoop())
					AnimRate() = 0.0f;
				CallEvent(this, "AnimEnd");
			}
			else
			{
				AnimFrame() = toFrame;
			}
		}
	}
}

/////////////////////////////////////////////////////////////////////////////

void UPawn::Tick(float elapsed, bool tickedFlag)
{
	UActor::Tick(elapsed, tickedFlag);

	if (bIsPlayer() && Role() >= ROLE_AutonomousProxy)
	{
		if (bViewTarget())
			CallEvent(this, "UpdateEyeHeight", { ExpressionValue::FloatValue(elapsed) });
		else
			ViewRotation() = Rotation();
	}

	if (Weapon())
		Weapon()->Location() = Location();

	if (Role() == ROLE_Authority)
	{
		if (PainTime() > 0.0f)
		{
			PainTime() = std::max(PainTime() - elapsed, 0.0f);
			if (PainTime() == 0.0f)
				CallEvent(this, "PainTimer");
		}
		if (SpeechTime() > 0.0f)
		{
			SpeechTime() = std::max(SpeechTime() - elapsed, 0.0f);
			if (SpeechTime() == 0.0f)
				CallEvent(this, "SpeechTimer");
		}
		if (bAdvancedTactics())
			CallEvent(this, "UpdateTactics", { ExpressionValue::FloatValue(elapsed) });
	}
}

void UPlayerPawn::Tick(float elapsed, bool tickedFlag)
{
	UPawn::Tick(elapsed, tickedFlag);

	if (Role() >= ROLE_SimulatedProxy)
	{
		if (Player() && !UObject::TryCast<UCamera>(this))
		{
			CallEvent(this, "PlayerInput", { ExpressionValue::FloatValue(elapsed) });
			CallEvent(this, "PlayerTick", { ExpressionValue::FloatValue(elapsed) });
		}
	}
}
