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

#pragma once

#include "../Container/ArrayPtr.h"
#include "../Container/Ptr.h"
#include "../Graphics/GraphicsDefs.h"
#include "../Graphics/Skeleton.h"
#include "../Math/BoundingBox.h"
#include "../Resource/Resource.h"

namespace Urho3D
{

class Geometry;
class IndexBuffer;
class Graphics;
class VertexBuffer;

/// Vertex buffer morph data.
struct VertexBufferMorph
{
    /// Vertex elements.
    VertexMaskFlags elementMask_;
    /// Number of vertices.
    unsigned vertexCount_;
    /// Morphed vertices data size as bytes.
    unsigned dataSize_;
    /// Morphed vertices. Stored packed as <index, data> pairs.
    SharedArrayPtr<unsigned char> morphData_;
};

/// Definition of a model's vertex morph.
struct ModelMorph
{
    /// Morph name.
    String name_;
    /// Morph name hash.
    StringHash nameHash_;
    /// Current morph weight.
    float weight_;
    /// Morph data per vertex buffer.
    HashMap<unsigned, VertexBufferMorph> buffers_;
};

/// Description of vertex buffer data for asynchronous loading.
struct VertexBufferDesc
{
    /// Vertex count.
    unsigned vertexCount_;
    /// Vertex declaration.
    PODVector<VertexElement> vertexElements_;
    /// Vertex data size.
    unsigned dataSize_;
    /// Vertex data.
    SharedArrayPtr<unsigned char> data_;
};

/// Description of index buffer data for asynchronous loading.
struct IndexBufferDesc
{
    /// Index count.
    unsigned indexCount_;
    /// Index size.
    unsigned indexSize_;
    /// Index data size.
    unsigned dataSize_;
    /// Index data.
    SharedArrayPtr<unsigned char> data_;
};

/// Description of a geometry for asynchronous loading.
struct GeometryDesc
{
    /// Primitive type.
    PrimitiveType type_;
    /// Vertex buffer ref.
    unsigned vbRef_;
    /// Index buffer ref.
    unsigned ibRef_;
    /// Index start.
    unsigned indexStart_;
    /// Index count.
    unsigned indexCount_;
};

/// 3D model resource.
class URHO3D_API Model : public ResourceWithMetadata
{
    URHO3D_OBJECT(Model, ResourceWithMetadata);

public:
    /// Construct.
    explicit Model(Context* context);
    /// Destruct.
    ~Model() override;
    /// Register object factory.
    /// @nobind
    static void RegisterObject(Context* context);

    /// Load resource from stream. May be called from a worker thread. Return true if successful.
    bool BeginLoad(Deserializer& source) override;
    /// Finish resource loading. Always called from the main thread. Return true if successful.
    bool EndLoad() override;
    /// Save resource. Return true if successful.
    bool Save(Serializer& dest) const override;

    /// Set local-space bounding box.
    /// @property
    void SetBoundingBox(const BoundingBox& box);
    /// Set vertex buffers and their morph ranges.
    bool SetVertexBuffers(const std::vector<SharedPtr<VertexBuffer> >& buffers, const PODVector<unsigned>& morphRangeStarts,
        const PODVector<unsigned>& morphRangeCounts);
    /// Set index buffers.
    bool SetIndexBuffers(const std::vector<SharedPtr<IndexBuffer> >& buffers);
    /// Set number of geometries.
    /// @property
    void SetNumGeometries(unsigned num);
    /// Set number of LOD levels in a geometry.
    /// @property
    bool SetNumGeometryLodLevels(unsigned index, unsigned num);
    /// Set geometry.
    bool SetGeometry(unsigned index, unsigned lodLevel, Geometry* geometry);
    /// Set geometry center.
    /// @property{set_geometryCenters}
    bool SetGeometryCenter(unsigned index, const Vector3& center);
    /// Set skeleton.
    void SetSkeleton(const Skeleton& skeleton);
    /// Set bone mappings when model has more bones than the skinning shader can handle.
    void SetGeometryBoneMappings(const std::vector<std::vector<u32> >& geometryBoneMappings);
    /// Set vertex morphs.
    void SetMorphs(const std::vector<ModelMorph>& morphs);
    /// Clone the model. The geometry data is deep-copied and can be modified in the clone without affecting the original.
    SharedPtr<Model> Clone(const String& cloneName = String::EMPTY) const;

    /// Return bounding box.
    /// @property
    const BoundingBox& GetBoundingBox() const { return boundingBox_; }

    /// Return skeleton.
    /// @property
    Skeleton& GetSkeleton() { return skeleton_; }

    /// Return vertex buffers.
    const std::vector<SharedPtr<VertexBuffer> >& GetVertexBuffers() const { return vertexBuffers_; }

    /// Return index buffers.
    const std::vector<SharedPtr<IndexBuffer> >& GetIndexBuffers() const { return indexBuffers_; }

    /// Return number of geometries.
    /// @property
    uint32_t GetNumGeometries() const { return (uint32_t)geometries_.size(); }

    /// Return number of LOD levels in geometry.
    /// @property
    unsigned GetNumGeometryLodLevels(unsigned index) const;

    /// Return geometry pointers.
    const std::vector<std::vector<SharedPtr<Geometry> > >& GetGeometries() const { return geometries_; }

    /// Return geometry center points.
    const PODVector<Vector3>& GetGeometryCenters() const { return geometryCenters_; }

    /// Return geometry by index and LOD level. The LOD level is clamped if out of range.
    Geometry* GetGeometry(unsigned index, unsigned lodLevel) const;

    /// Return geometry center by index.
    /// @property{get_geometryCenters}
    const Vector3& GetGeometryCenter(unsigned index) const
    {
        return index < geometryCenters_.Size() ? geometryCenters_[index] : Vector3::ZERO;
    }

    /// Return geometery bone mappings.
    const std::vector<std::vector<u32> >& GetGeometryBoneMappings() const { return geometryBoneMappings_; }

    /// Return vertex morphs.
    const std::vector<ModelMorph>& GetMorphs() const { return morphs_; }

    /// Return number of vertex morphs.
    /// @property
    uint32_t GetNumMorphs() const { return (uint32_t)morphs_.size(); }

    /// Return vertex morph by index.
    const ModelMorph* GetMorph(uint32_t index) const;
    /// Return vertex morph by name.
    const ModelMorph* GetMorph(const String& name) const;
    /// Return vertex morph by name hash.
    const ModelMorph* GetMorph(StringHash nameHash) const;
    /// Return vertex buffer morph range start.
    unsigned GetMorphRangeStart(unsigned bufferIndex) const;
    /// Return vertex buffer morph range vertex count.
    unsigned GetMorphRangeCount(unsigned bufferIndex) const;

private:
    /// Bounding box.
    BoundingBox boundingBox_;
    /// Skeleton.
    Skeleton skeleton_;
    /// Vertex buffers.
    std::vector<SharedPtr<VertexBuffer> > vertexBuffers_;
    /// Index buffers.
    std::vector<SharedPtr<IndexBuffer> > indexBuffers_;
    /// Geometries.
    std::vector<std::vector<SharedPtr<Geometry> > > geometries_;
    /// Geometry bone mappings.
    std::vector<std::vector<u32> > geometryBoneMappings_;
    /// Geometry centers.
    PODVector<Vector3> geometryCenters_;
    /// Vertex morphs.
    std::vector<ModelMorph> morphs_;
    /// Vertex buffer morph range start.
    PODVector<unsigned> morphRangeStarts_;
    /// Vertex buffer morph range vertex count.
    PODVector<unsigned> morphRangeCounts_;
    /// Vertex buffer data for asynchronous loading.
    std::vector<VertexBufferDesc> loadVBData_;
    /// Index buffer data for asynchronous loading.
    std::vector<IndexBufferDesc> loadIBData_;
    /// Geometry definitions for asynchronous loading.
    std::vector<PODVector<GeometryDesc> > loadGeometries_;
};

}
