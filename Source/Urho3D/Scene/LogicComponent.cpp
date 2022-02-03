//
// Copyright (c) 2008-2022 the Urho3D project.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//

#include "../Precompiled.h"

#include "../IO/Log.h"
#if defined(URHO3D_PHYSICS) || defined(URHO3D_PHYSICS2D)
#include "../Physics/PhysicsEvents.h"
#endif
#include "../Scene/LogicComponent.h"
#include "../Scene/Scene.h"
#include "../Scene/SceneEvents.h"

using namespace Urho3D;

LogicComponent::LogicComponent(Context* context)
    : Component(context)
    , updateEventMask_(UpdateEventFlags::Update | UpdateEventFlags::PostUpdate | UpdateEventFlags::FixedUpdate | UpdateEventFlags::FixedPostUpdate)
{
}

void LogicComponent::OnSetEnabled()
{
    UpdateEventSubscription();
}

void LogicComponent::Update(float timeStep)
{
}

void LogicComponent::PostUpdate(float timeStep)
{
}

void LogicComponent::FixedUpdate(float timeStep)
{
}

void LogicComponent::FixedPostUpdate(float timeStep)
{
}

void LogicComponent::SetUpdateEventMask(UpdateEventFlags mask)
{
    if (updateEventMask_ != mask)
    {
        updateEventMask_ = mask;
        UpdateEventSubscription();
    }
}

void LogicComponent::OnNodeSet(Node * node)
{
    if (node)
    {
        // Execute the user-defined start function
        Start();
    }
    else
    {
        // We are being detached from a node: execute user-defined stop function and prepare for destruction
        Stop();
    }
}

void LogicComponent::OnSceneSet(Scene * scene)
{
    if (scene)
        UpdateEventSubscription();
    else
    {
        UnsubscribeFromEvent(E_SCENEUPDATE);
        UnsubscribeFromEvent(E_SCENEPOSTUPDATE);
#if defined(URHO3D_PHYSICS) || defined(URHO3D_PHYSICS2D)
        UnsubscribeFromEvent(E_PHYSICSPRESTEP);
        UnsubscribeFromEvent(E_PHYSICSPOSTSTEP);
#endif
        currentEventMask_ = UpdateEventFlags::None;
    }
}

void LogicComponent::UpdateEventSubscription()
{
    Scene* scene = GetScene();
    if (!scene)
        return;

    bool enabled = IsEnabledEffective();

    bool needUpdate = enabled && ((updateEventMask_ & UpdateEventFlags::Update) != 0 || !delayedStartCalled_);
    if (needUpdate && ((currentEventMask_ & UpdateEventFlags::Update) == 0))
    {
        SubscribeToEvent(scene, E_SCENEUPDATE, URHO3D_HANDLER(LogicComponent, HandleSceneUpdate));
        currentEventMask_ |= UpdateEventFlags::Update;
    }
    else if (!needUpdate && ((currentEventMask_ & UpdateEventFlags::Update) != 0))
    {
        UnsubscribeFromEvent(scene, E_SCENEUPDATE);
        currentEventMask_ &= ~UpdateEventFlags::Update;
    }

    bool needPostUpdate = enabled && ((updateEventMask_ & UpdateEventFlags::PostUpdate) != 0);
    if (needPostUpdate && ((currentEventMask_ & UpdateEventFlags::PostUpdate) == 0))
    {
        SubscribeToEvent(scene, E_SCENEPOSTUPDATE, URHO3D_HANDLER(LogicComponent, HandleScenePostUpdate));
        currentEventMask_ |= UpdateEventFlags::PostUpdate;
    }
    else if (!needPostUpdate && ((currentEventMask_ & UpdateEventFlags::PostUpdate) != 0))
    {
        UnsubscribeFromEvent(scene, E_SCENEPOSTUPDATE);
        currentEventMask_ &= ~UpdateEventFlags::PostUpdate;
    }

#if defined(URHO3D_PHYSICS) || defined(URHO3D_PHYSICS2D)
    Component* world = GetFixedUpdateSource();
    if (!world)
        return;

    bool needFixedUpdate = enabled && ((updateEventMask_ & UpdateEventFlags::FixedUpdate) != 0);
    if (needFixedUpdate && ((currentEventMask_ & UpdateEventFlags::FixedUpdate) == 0))
    {
        SubscribeToEvent(world, E_PHYSICSPRESTEP, URHO3D_HANDLER(LogicComponent, HandlePhysicsPreStep));
        currentEventMask_ |= UpdateEventFlags::FixedUpdate;
    }
    else if (!needFixedUpdate && ((currentEventMask_ & UpdateEventFlags::FixedUpdate) != 0))
    {
        UnsubscribeFromEvent(world, E_PHYSICSPRESTEP);
        currentEventMask_ &= ~UpdateEventFlags::FixedUpdate;
    }

    bool needFixedPostUpdate = enabled && ((updateEventMask_ & UpdateEventFlags::FixedPostUpdate) != 0);
    if (needFixedPostUpdate && ((currentEventMask_ & UpdateEventFlags::FixedPostUpdate) == 0))
    {
        SubscribeToEvent(world, E_PHYSICSPOSTSTEP, URHO3D_HANDLER(LogicComponent, HandlePhysicsPostStep));
        currentEventMask_ |= UpdateEventFlags::FixedPostUpdate;
    }
    else if (!needFixedPostUpdate && ((currentEventMask_ & UpdateEventFlags::FixedPostUpdate) != 0))
    {
        UnsubscribeFromEvent(world, E_PHYSICSPOSTSTEP);
        currentEventMask_ &= ~UpdateEventFlags::FixedPostUpdate;
    }
#endif
}

void LogicComponent::HandleSceneUpdate(StringHash eventType, VariantMap & eventData)
{
    using namespace SceneUpdate;

    // Execute user-defined delayed start function before first update
    if (!delayedStartCalled_)
    {
        DelayedStart();
        delayedStartCalled_ = true;

        // If did not need actual update events, unsubscribe now
        if ((updateEventMask_ & UpdateEventFlags::Update) == UpdateEventFlags::None)
        {
            UnsubscribeFromEvent(GetScene(), E_SCENEUPDATE);
            currentEventMask_ &= ~UpdateEventFlags::Update;
            return;
        }
    }

    // Then execute user-defined update function
    Update(eventData[P_TIMESTEP].GetFloat());
}

void LogicComponent::HandleScenePostUpdate(StringHash eventType, VariantMap & eventData)
{
    using namespace ScenePostUpdate;

    // Execute user-defined post-update function
    PostUpdate(eventData[P_TIMESTEP].GetFloat());
}

#if defined(URHO3D_PHYSICS) || defined(URHO3D_PHYSICS2D)

void LogicComponent::HandlePhysicsPreStep(StringHash eventType, VariantMap & eventData)
{
    using namespace PhysicsPreStep;

    // Execute user-defined delayed start function before first fixed update if not called yet
    if (!delayedStartCalled_)
    {
        DelayedStart();
        delayedStartCalled_ = true;
    }

    // Execute user-defined fixed update function
    FixedUpdate(eventData[P_TIMESTEP].GetFloat());
}

void LogicComponent::HandlePhysicsPostStep(StringHash eventType, VariantMap & eventData)
{
    using namespace PhysicsPostStep;

    // Execute user-defined fixed post-update function
    FixedPostUpdate(eventData[P_TIMESTEP].GetFloat());
}

#endif
