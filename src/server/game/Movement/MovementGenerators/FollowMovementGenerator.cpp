/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "FollowMovementGenerator.h"
#include "CreatureAI.h"
#include "MoveSpline.h"
#include "MoveSplineInit.h"
#include "PathGenerator.h"
#include "Pet.h"
#include "Unit.h"
#include "Util.h"

FollowMovementGenerator::FollowMovementGenerator(Unit* target, float range, ChaseAngle angle) : AbstractFollower(ASSERT_NOTNULL(target)), _range(range), _angle(angle) {}
FollowMovementGenerator::~FollowMovementGenerator() {}

static void DoMovementInform(Unit* owner, Unit* target)
{
    if (owner->GetTypeId() != TYPEID_UNIT)
        return;
    if (UnitAI* ai = owner->GetAI())
        static_cast<CreatureAI*>(ai)->MovementInform(FOLLOW_MOTION_TYPE, target->GetGUID().GetCounter());
}

static bool PositionOkay(Unit* owner, Unit* target, float range, Optional<ChaseAngle> angle = {})
{
    if (owner->GetExactDistSq(target) > square(owner->GetCombatReach() + target->GetCombatReach() + range))
        return false;
    return !angle || angle->IsAngleOkay(target->GetRelativeAngle(owner));
}

void FollowMovementGenerator::Initialize(Unit* owner)
{
    owner->AddUnitState(UNIT_STATE_FOLLOW);
    UpdatePetSpeed(owner);
    _lastTargetPosition.reset();
}

bool FollowMovementGenerator::Update(Unit* owner, uint32 diff)
{
    // owner might be dead or gone
    if (!owner->IsAlive())
        return false;

    // our target might have gone away
    Unit* const target = GetTarget();
    if (!target)
        return false;

    if (owner->HasUnitState(UNIT_STATE_NOT_MOVE) || owner->IsMovementPreventedByCasting())
    {
        owner->StopMoving();
        _lastTargetPosition.reset();
        return true;
    }

    if (owner->HasUnitState(UNIT_STATE_FOLLOW_MOVE))
    {
        if (_checkTimer > diff)
            _checkTimer -= diff;
        else
        {
            _checkTimer = CHECK_INTERVAL;
            if (PositionOkay(owner, target, _range, _angle))
            {
                _path = nullptr;
                owner->StopMoving();
                DoMovementInform(owner, target);
                return true;
            }
        }
    }

    if (owner->HasUnitState(UNIT_STATE_FOLLOW_MOVE) && owner->movespline->Finalized())
    {
        _path = nullptr;
        owner->ClearUnitState(UNIT_STATE_FOLLOW_MOVE);
        DoMovementInform(owner, target);
    }

    if (!_lastTargetPosition || _lastTargetPosition->GetExactDistSq(target->GetPosition()) > 0.0f)
    {
        _lastTargetPosition = target->GetPosition();
        if (owner->HasUnitState(UNIT_STATE_FOLLOW_MOVE) || !PositionOkay(owner, target, _range + FOLLOW_RANGE_TOLERANCE))
        {
            if (!_path)
                _path = std::make_unique<PathGenerator>(owner);

            float x, y, z;

            // select angle
            float tAngle;
            float const curAngle = target->GetRelativeAngle(owner);
            if (_angle.IsAngleOkay(curAngle))
                tAngle = curAngle;
            else
            {
                float const diffUpper = Position::NormalizeOrientation(curAngle - _angle.UpperBound());
                float const diffLower = Position::NormalizeOrientation(_angle.LowerBound() - curAngle);
                if (diffUpper < diffLower)
                    tAngle = _angle.UpperBound();
                else
                    tAngle = _angle.LowerBound();
            }

            target->GetNearPoint(owner, x, y, z, _range, target->NormalizeOrientation(target->GetOrientation() + tAngle));

            if (owner->IsHovering())
                owner->UpdateAllowedPositionZ(x, y, z);

            // pets are allowed to "cheat" on pathfinding when following their master
            bool allowShortcut = false;
            if (Pet* oPet = owner->ToPet())
                if (target->GetGUID() == oPet->GetOwnerGUID())
                    allowShortcut = true;

            bool success = _path->CalculatePath(x, y, z, allowShortcut);
            if (!success || (_path->GetPathType() & PATHFIND_NOPATH))
            {
                owner->StopMoving();
                return true;
            }

            owner->AddUnitState(UNIT_STATE_FOLLOW_MOVE);

            Movement::MoveSplineInit init(owner);
            init.MovebyPath(_path->GetPath());
            init.SetWalk(target->IsWalking());
            init.SetFacing(target->GetOrientation());
            init.Launch();
        }
    }
    return true;
}

void FollowMovementGenerator::Finalize(Unit* owner)
{
    owner->ClearUnitState(UNIT_STATE_FOLLOW | UNIT_STATE_FOLLOW_MOVE);
    UpdatePetSpeed(owner);
}

void FollowMovementGenerator::UpdatePetSpeed(Unit* owner)
{
    if (Pet* oPet = owner->ToPet())
        if (!GetTarget() || GetTarget()->GetGUID() == owner->GetOwnerGUID())
        {
            oPet->UpdateSpeed(MOVE_RUN);
            oPet->UpdateSpeed(MOVE_WALK);
            oPet->UpdateSpeed(MOVE_SWIM);
        }
}
