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
#include "../Scene/Component.h"
#include "../Scene/SceneResolver.h"
#include "../Scene/Node.h"

#include "../DebugNew.h"
#include <unordered_set>

using namespace Urho3D;

void SceneResolver::Reset()
{
    nodes_.clear();
    components_.clear();
}

void SceneResolver::AddNode(uint32_t oldID, Node* node)
{
    if (node)
        nodes_[oldID] = node;
}

void SceneResolver::AddComponent(uint32_t oldID, Component* component)
{
    if (component)
        components_[oldID] = component;
}

void SceneResolver::Resolve()
{
    // Nodes do not have component or node ID attributes, so only have to go through components
    std::unordered_set<StringHash> noIDAttributes;
    for (auto i = components_.begin(); i != components_.end(); ++i)
    {
        Component* component = i->second;
        if (!component || noIDAttributes.find(component->GetType()) != noIDAttributes.end())
            continue;

        bool hasIDAttributes = false;
        const std::vector<AttributeInfo>* attributes = component->GetAttributes();
        if (!attributes)
        {
            noIDAttributes.insert(component->GetType());
            continue;
        }

        for (uint32_t j = 0; j < attributes->size(); ++j)
        {
            const AttributeInfo& info = attributes->at(j);
            if (info.mode_ & AM_NODEID)
            {
                hasIDAttributes = true;
                uint32_t oldNodeID = component->GetAttribute(j).GetUInt();

                if (oldNodeID)
                {
                    std::unordered_map<uint32_t, WeakPtr<Node> >::const_iterator k = nodes_.find(oldNodeID);

                    if (k != nodes_.end() && k->second)
                    {
                        uint32_t newNodeID = k->second->GetID();
                        component->SetAttribute(j, Variant(newNodeID));
                    }
                    else
                    {
                        URHO3D_LOGWARNING("Could not resolve node ID " + String(oldNodeID));
                    }
                }
            }
            else if (info.mode_ & AM_COMPONENTID)
            {
                hasIDAttributes = true;
                uint32_t oldComponentID = component->GetAttribute(j).GetUInt();

                if (oldComponentID)
                {
                    std::unordered_map<uint32_t, WeakPtr<Component> >::const_iterator k = components_.find(oldComponentID);

                    if (k != components_.end() && k->second)
                    {
                        uint32_t newComponentID = k->second->GetID();
                        component->SetAttribute(j, Variant(newComponentID));
                    }
                    else
                    {
                        URHO3D_LOGWARNING("Could not resolve component ID " + String(oldComponentID));
                    }
                }
            }
            else if (info.mode_ & AM_NODEIDVECTOR)
            {
                hasIDAttributes = true;
                Variant attrValue = component->GetAttribute(j);
                const VariantVector& oldNodeIDs = attrValue.GetVariantVector();

                if (oldNodeIDs.size())
                {
                    // The first index stores the number of IDs redundantly. This is for editing
                    uint32_t numIDs = oldNodeIDs[0].GetUInt();
                    VariantVector newIDs;
                    newIDs.push_back(numIDs);

                    for (size_t k = 1; k < oldNodeIDs.size(); ++k)
                    {
                        uint32_t oldNodeID = oldNodeIDs[k].GetUInt();
                        std::unordered_map<uint32_t, WeakPtr<Node> >::const_iterator l = nodes_.find(oldNodeID);

                        if (l != nodes_.end() && l->second)
                        {
                            newIDs.push_back(l->second->GetID());
                        }
                        else
                        {
                            // If node was not found, retain number of elements, just store ID 0
                            newIDs.push_back(0);
                            URHO3D_LOGWARNING("Could not resolve node ID " + String(oldNodeID));
                        }
                    }

                    component->SetAttribute(j, newIDs);
                }
            }
        }

        // If component type had no ID attributes, cache this fact for optimization
        if (!hasIDAttributes)
        {
            noIDAttributes.insert(component->GetType());
        }
    }

    // Attributes have been resolved, so no need to remember the nodes after this
    Reset();
}
