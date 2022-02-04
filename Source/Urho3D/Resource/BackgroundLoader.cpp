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

#ifdef URHO3D_THREADING

#include "../Precompiled.h"

#include "../Core/Context.h"
#include "../Core/Profiler.h"
#include "../IO/Log.h"
#include "../Resource/BackgroundLoader.h"
#include "../Resource/ResourceCache.h"
#include "../Resource/ResourceEvents.h"

#include "../DebugNew.h"

using namespace std;
using namespace Urho3D;

BackgroundLoader::BackgroundLoader(ResourceCache* owner) :
    owner_(owner)
{
}

BackgroundLoader::~BackgroundLoader()
{
    lock_guard<mutex> lock(backgroundLoadMutex);

    backgroundLoadQueue_.clear();
}

void BackgroundLoader::ThreadFunction()
{
    URHO3D_PROFILE_THREAD("BackgroundLoader Thread");

    while (shouldRun_)
    {
        backgroundLoadMutex.lock();

        // Search for a queued resource that has not been loaded yet
        auto i = backgroundLoadQueue_.begin();
        while (i != backgroundLoadQueue_.end())
        {
            if (i->second.resource_->GetAsyncLoadState() == ASYNC_QUEUED)
                break;
            else
                ++i;
        }

        if (i == backgroundLoadQueue_.end())
        {
            // No resources to load found
            backgroundLoadMutex.unlock();
            Time::Sleep(5);
        }
        else
        {
            BackgroundLoadItem& item = i->second;
            Resource* resource = item.resource_;
            // We can be sure that the item is not removed from the queue as long as it is in the
            // "queued" or "loading" state
            backgroundLoadMutex.unlock();

            bool success = false;
            SharedPtr<File> file = owner_->GetFile(resource->GetName(), item.sendEventOnFailure_);
            if (file)
            {
                resource->SetAsyncLoadState(ASYNC_LOADING);
                success = resource->BeginLoad(*file);
            }

            // Process dependencies now
            // Need to lock the queue again when manipulating other entries
            std::pair<StringHash, StringHash> key = std::make_pair(resource->GetType(), resource->GetNameHash());
            backgroundLoadMutex.lock();
            if (item.dependents_.size())
            {
                for (std::unordered_set<std::pair<StringHash, StringHash> >::iterator i = item.dependents_.begin();
                    i != item.dependents_.end(); ++i)
                {
                    auto j = backgroundLoadQueue_.find(*i);
                    if (j != backgroundLoadQueue_.end())
                    {
                        j->second.dependencies_.erase(key);
                    }
                }

                item.dependents_.clear();
            }

            resource->SetAsyncLoadState(success ? ASYNC_SUCCESS : ASYNC_FAIL);
            backgroundLoadMutex.unlock();
        }
    }
}

bool BackgroundLoader::QueueResource(StringHash type, const String& name, bool sendEventOnFailure, Resource* caller)
{
    StringHash nameHash(name);
    std::pair<StringHash, StringHash> key = std::make_pair(type, nameHash);

    lock_guard<mutex> lock(backgroundLoadMutex);

    // Check if already exists in the queue
    if (backgroundLoadQueue_.find(key) != backgroundLoadQueue_.end())
        return false;

    BackgroundLoadItem& item = backgroundLoadQueue_[key];
    item.sendEventOnFailure_ = sendEventOnFailure;

    // Make sure the pointer is non-null and is a Resource subclass
    item.resource_ = DynamicCast<Resource>(owner_->GetContext()->CreateObject(type));
    if (!item.resource_)
    {
        URHO3D_LOGERROR("Could not load unknown resource type " + String(type));

        if (sendEventOnFailure && Thread::IsMainThread())
        {
            using namespace UnknownResourceType;

            VariantMap& eventData = owner_->GetEventDataMap();
            eventData[P_RESOURCETYPE] = type;
            owner_->SendEvent(E_UNKNOWNRESOURCETYPE, eventData);
        }

        backgroundLoadQueue_.erase(key);
        return false;
    }

    URHO3D_LOGDEBUG("Background loading resource " + name);

    item.resource_->SetName(name);
    item.resource_->SetAsyncLoadState(ASYNC_QUEUED);

    // If this is a resource calling for the background load of more resources, mark the dependency as necessary
    if (caller)
    {
        std::pair<StringHash, StringHash> callerKey = std::make_pair(caller->GetType(), caller->GetNameHash());
        auto j = backgroundLoadQueue_.find(callerKey);
        if (j != backgroundLoadQueue_.end())
        {
            BackgroundLoadItem& callerItem = j->second;
            item.dependents_.insert(callerKey);
            callerItem.dependencies_.insert(key);
        }
        else
            URHO3D_LOGWARNING("Resource " + caller->GetName() +
                " requested for a background loaded resource but was not in the background load queue");
    }

    // Start the background loader thread now
    if (!IsStarted())
        Run();

    return true;
}

void BackgroundLoader::WaitForResource(StringHash type, StringHash nameHash)
{
    backgroundLoadMutex.lock();

    // Check if the resource in question is being background loaded
    std::pair<StringHash, StringHash> key = std::make_pair(type, nameHash);
    auto i = backgroundLoadQueue_.find(key);
    if (i != backgroundLoadQueue_.end())
    {
        backgroundLoadMutex.unlock();

        {
            Resource* resource = i->second.resource_;
            HiresTimer waitTimer;
            bool didWait = false;

            for (;;)
            {
                size_t numDeps = i->second.dependencies_.size();
                AsyncLoadState state = resource->GetAsyncLoadState();
                if (numDeps > 0 || state == ASYNC_QUEUED || state == ASYNC_LOADING)
                {
                    didWait = true;
                    Time::Sleep(1);
                }
                else
                    break;
            }

            if (didWait)
                URHO3D_LOGDEBUG("Waited " + String(waitTimer.GetUSec(false) / 1000) + " ms for background loaded resource " +
                    resource->GetName());
        }

        // This may take a long time and may potentially wait on other resources, so it is important we do not hold the mutex during this
        FinishBackgroundLoading(i->second);

        backgroundLoadMutex.lock();
        backgroundLoadQueue_.erase(i);
        backgroundLoadMutex.unlock();
    }
    else
    {
        backgroundLoadMutex.unlock();
    }
}

void BackgroundLoader::FinishResources(int maxMs)
{
    if (IsStarted())
    {
        HiresTimer timer;

        backgroundLoadMutex.lock();

        for (auto i = backgroundLoadQueue_.begin(); i != backgroundLoadQueue_.end();)
        {
            Resource* resource = i->second.resource_;
            size_t numDeps = i->second.dependencies_.size();
            AsyncLoadState state = resource->GetAsyncLoadState();
            if (numDeps > 0 || state == ASYNC_QUEUED || state == ASYNC_LOADING)
            {
                ++i;
            }
            else
            {
                // Finishing a resource may need it to wait for other resources to load, in which case we can not
                // hold on to the mutex
                backgroundLoadMutex.unlock();
                FinishBackgroundLoading(i->second);
                backgroundLoadMutex.lock();
                i = backgroundLoadQueue_.erase(i);
            }

            // Break when the time limit passed so that we keep sufficient FPS
            if (timer.GetUSec(false) >= maxMs * 1000LL)
                break;
        }

        backgroundLoadMutex.unlock();
    }
}

uint32_t BackgroundLoader::GetNumQueuedResources() const
{
    lock_guard<mutex> lock(backgroundLoadMutex);
    return (uint32_t)backgroundLoadQueue_.size();
}

void BackgroundLoader::FinishBackgroundLoading(BackgroundLoadItem& item)
{
    Resource* resource = item.resource_;

    bool success = resource->GetAsyncLoadState() == ASYNC_SUCCESS;
    // If BeginLoad() phase was successful, call EndLoad() and get the final success/failure result
    if (success)
    {
#ifdef URHO3D_TRACY_PROFILING
        URHO3D_PROFILE_COLOR(FinishBackgroundLoading, URHO3D_PROFILE_RESOURCE_COLOR);

        String profileBlockName("Finish" + resource->GetTypeName());
        URHO3D_PROFILE_STR(profileBlockName.CString(), profileBlockName.Length());
#elif defined(URHO3D_PROFILING)
        String profileBlockName("Finish" + resource->GetTypeName());

        auto* profiler = owner_->GetSubsystem<Profiler>();
        if (profiler)
            profiler->BeginBlock(profileBlockName.CString());
#endif

        URHO3D_LOGDEBUG("Finishing background loaded resource " + resource->GetName());
        success = resource->EndLoad();

#ifdef URHO3D_PROFILING
        if (profiler)
            profiler->EndBlock();
#endif
    }
    resource->SetAsyncLoadState(ASYNC_DONE);

    if (!success && item.sendEventOnFailure_)
    {
        using namespace LoadFailed;

        VariantMap& eventData = owner_->GetEventDataMap();
        eventData[P_RESOURCENAME] = resource->GetName();
        owner_->SendEvent(E_LOADFAILED, eventData);
    }

    // Store to the cache just before sending the event; use same mechanism as for manual resources
    if (success || owner_->GetReturnFailedResources())
        owner_->AddManualResource(resource);

    // Send event, either success or failure
    {
        using namespace ResourceBackgroundLoaded;

        VariantMap& eventData = owner_->GetEventDataMap();
        eventData[P_RESOURCENAME] = resource->GetName();
        eventData[P_SUCCESS] = success;
        eventData[P_RESOURCE] = resource;
        owner_->SendEvent(E_RESOURCEBACKGROUNDLOADED, eventData);
    }
}

#endif
