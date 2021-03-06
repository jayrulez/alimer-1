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

#include "../Graphics/Skeleton.h"
#include "../IO/Log.h"

#include "../DebugNew.h"

using namespace std;
using namespace Urho3D;

Skeleton::Skeleton() :
    rootBoneIndex_(M_MAX_UNSIGNED)
{
}

bool Skeleton::Load(Deserializer& source)
{
    ClearBones();

    if (source.IsEof())
        return false;

    unsigned bones = source.ReadUInt();
    bones_.reserve(bones);

    for (unsigned i = 0; i < bones; ++i)
    {
        Bone newBone;
        newBone.name_ = source.ReadString();
        newBone.nameHash_ = newBone.name_;
        newBone.parentIndex_ = source.ReadUInt();
        newBone.initialPosition_ = source.ReadVector3();
        newBone.initialRotation_ = source.ReadQuaternion();
        newBone.initialScale_ = source.ReadVector3();
        source.Read(&newBone.offsetMatrix_.m00_, sizeof(Matrix3x4));

        // Read bone collision data
        newBone.collisionMask_ = BoneCollisionShapeFlags(source.ReadUByte());
        if ((newBone.collisionMask_ & BoneCollisionShapeFlags::Sphere) != 0)
            newBone.radius_ = source.ReadFloat();
        if ((newBone.collisionMask_ & BoneCollisionShapeFlags::Box) != 0)
            newBone.boundingBox_ = source.ReadBoundingBox();

        if (newBone.parentIndex_ == i)
            rootBoneIndex_ = i;

        bones_.push_back(newBone);
    }

    return true;
}

bool Skeleton::Save(Serializer& dest) const
{
    if (!dest.WriteUInt((uint32_t)bones_.size()))
        return false;

    for (size_t i = 0; i < bones_.size(); ++i)
    {
        const Bone& bone = bones_[i];
        dest.WriteString(bone.name_);
        dest.WriteUInt(bone.parentIndex_);
        dest.WriteVector3(bone.initialPosition_);
        dest.WriteQuaternion(bone.initialRotation_);
        dest.WriteVector3(bone.initialScale_);
        dest.Write(bone.offsetMatrix_.Data(), sizeof(Matrix3x4));

        // Collision info
        dest.WriteUByte((uint8_t)bone.collisionMask_);
        if ((bone.collisionMask_ & BoneCollisionShapeFlags::Sphere) != 0)
            dest.WriteFloat(bone.radius_);
        if ((bone.collisionMask_ & BoneCollisionShapeFlags::Box) != 0)
            dest.WriteBoundingBox(bone.boundingBox_);
    }

    return true;
}

void Skeleton::Define(const Skeleton& src)
{
    ClearBones();

    bones_ = src.bones_;
    // Make sure we clear node references, if they exist
    // (AnimatedModel will create new nodes on its own)
    for (vector<Bone>::iterator i = bones_.begin(); i != bones_.end(); ++i)
        i->node_.Reset();
    rootBoneIndex_ = src.rootBoneIndex_;
}

void Skeleton::SetRootBoneIndex(uint32_t index)
{
    if (index < bones_.size())
        rootBoneIndex_ = index;
    else
        URHO3D_LOGERROR("Root bone index out of bounds");
}

void Skeleton::ClearBones()
{
    bones_.clear();
    rootBoneIndex_ = M_MAX_UNSIGNED;
}

void Skeleton::Reset()
{
    for (vector<Bone>::iterator i = bones_.begin(); i != bones_.end(); ++i)
    {
        if (i->animated_ && i->node_)
            i->node_->SetTransform(i->initialPosition_, i->initialRotation_, i->initialScale_);
    }
}

void Skeleton::ResetSilent()
{
    for (vector<Bone>::iterator i = bones_.begin(); i != bones_.end(); ++i)
    {
        if (i->animated_ && i->node_)
            i->node_->SetTransformSilent(i->initialPosition_, i->initialRotation_, i->initialScale_);
    }
}


Bone* Skeleton::GetRootBone()
{
    return GetBone(rootBoneIndex_);
}

uint32_t Skeleton::GetBoneIndex(const StringHash& boneNameHash) const
{
    const size_t numBones = bones_.size();
    for (size_t i = 0; i < numBones; ++i)
    {
        if (bones_[i].nameHash_ == boneNameHash)
            return (uint32_t)i;
    }

    return M_MAX_UNSIGNED;
}

uint32_t Skeleton::GetBoneIndex(const Bone* bone) const
{
    if (bones_.empty() || bone < &bones_.front() || bone > &bones_.back())
        return M_MAX_UNSIGNED;

    return static_cast<uint32_t>(bone - &bones_.front());
}

unsigned Skeleton::GetBoneIndex(const String& boneName) const
{
    return GetBoneIndex(StringHash(boneName));
}

Bone* Skeleton::GetBoneParent(const Bone* bone)
{
    if (GetBoneIndex(bone) == bone->parentIndex_)
        return nullptr;
    else
        return GetBone(bone->parentIndex_);
}

Bone* Skeleton::GetBone(unsigned index)
{
    return index < bones_.size() ? &bones_[index] : nullptr;
}

Bone* Skeleton::GetBone(const String& name)
{
    return GetBone(StringHash(name));
}

Bone* Skeleton::GetBone(const char* name)
{
    return GetBone(StringHash(name));
}

Bone* Skeleton::GetBone(const StringHash& boneNameHash)
{
    const unsigned index = GetBoneIndex(boneNameHash);
    return index < bones_.size() ? &bones_[index] : nullptr;
}
