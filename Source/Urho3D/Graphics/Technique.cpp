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

#include "../Core/Context.h"
#include "../Core/ProcessUtils.h"
#include "../Core/Profiler.h"
#include "../Graphics/Graphics.h"
#include "../Graphics/Technique.h"
#include "../Graphics/ShaderVariation.h"
#include "../IO/Log.h"
#include "../Resource/ResourceCache.h"
#include "../Resource/XMLFile.h"

#include "../DebugNew.h"

namespace Urho3D
{
    extern const char* cullModeNames[];

    const char* blendModeNames[] =
    {
        "replace",
        "add",
        "multiply",
        "alpha",
        "addalpha",
        "premulalpha",
        "invdestalpha",
        "subtract",
        "subtractalpha",
        nullptr
    };

    static const char* compareModeNames[] =
    {
        "always",
        "equal",
        "notequal",
        "less",
        "lessequal",
        "greater",
        "greaterequal",
        nullptr
    };

    static const char* lightingModeNames[] =
    {
        "unlit",
        "pervertex",
        "perpixel",
        nullptr
    };

    Pass::Pass(const String& name)
        : blendMode_(BLEND_REPLACE)
        , cullMode_(CullMode::Count)
        , depthTestMode_(CMP_LESSEQUAL)
        , shadersLoadedFrameNumber_(0)
        , alphaToCoverage_(false)
        , depthWrite_(true)
        , isDesktop_(false)
    {
        name_ = name.ToLower();
        index_ = Technique::GetPassIndex(name_);

        // Guess default lighting mode from pass name
        if (index_ == Technique::basePassIndex || index_ == Technique::alphaPassIndex || index_ == Technique::materialPassIndex ||
            index_ == Technique::deferredPassIndex)
            lightingMode_ = PassLightingMode::PerVertex;
        else if (index_ == Technique::lightPassIndex || index_ == Technique::litBasePassIndex || index_ == Technique::litAlphaPassIndex)
            lightingMode_ = PassLightingMode::PerPixel;
    }

    Pass::~Pass() = default;

    void Pass::SetBlendMode(BlendMode mode)
    {
        blendMode_ = mode;
    }

    void Pass::SetCullMode(CullMode mode)
    {
        cullMode_ = mode;
    }

    void Pass::SetDepthTestMode(CompareMode mode)
    {
        depthTestMode_ = mode;
    }

    void Pass::SetLightingMode(PassLightingMode mode)
    {
        lightingMode_ = mode;
    }

    void Pass::SetDepthWrite(bool enable)
    {
        depthWrite_ = enable;
    }

    void Pass::SetAlphaToCoverage(bool enable)
    {
        alphaToCoverage_ = enable;
    }


    void Pass::SetIsDesktop(bool enable)
    {
        isDesktop_ = enable;
    }

    void Pass::SetVertexShader(const String& name)
    {
        vertexShaderName_ = name;
        ReleaseShaders();
    }

    void Pass::SetPixelShader(const String& name)
    {
        pixelShaderName_ = name;
        ReleaseShaders();
    }

    void Pass::SetVertexShaderDefines(const String& defines)
    {
        vertexShaderDefines_ = defines;
        ReleaseShaders();
    }

    void Pass::SetPixelShaderDefines(const String& defines)
    {
        pixelShaderDefines_ = defines;
        ReleaseShaders();
    }

    void Pass::SetVertexShaderDefineExcludes(const String& excludes)
    {
        vertexShaderDefineExcludes_ = excludes;
        ReleaseShaders();
    }

    void Pass::SetPixelShaderDefineExcludes(const String& excludes)
    {
        pixelShaderDefineExcludes_ = excludes;
        ReleaseShaders();
    }

    void Pass::ReleaseShaders()
    {
        vertexShaders_.clear();
        pixelShaders_.clear();
        extraVertexShaders_.clear();
        extraPixelShaders_.clear();
    }

    void Pass::MarkShadersLoaded(unsigned frameNumber)
    {
        shadersLoadedFrameNumber_ = frameNumber;
    }

    String Pass::GetEffectiveVertexShaderDefines() const
    {
        // Prefer to return just the original defines if possible
        if (vertexShaderDefineExcludes_.Empty())
            return vertexShaderDefines_;

        std::vector<String> vsDefines = vertexShaderDefines_.Split(' ');
        std::vector<String> vsExcludes = vertexShaderDefineExcludes_.Split(' ');
        for (unsigned i = 0; i < vsExcludes.size(); ++i)
            vsDefines.erase(vsExcludes.begin() + i);

        return String::Joined(vsDefines, " ");
    }

    String Pass::GetEffectivePixelShaderDefines() const
    {
        // Prefer to return just the original defines if possible
        if (pixelShaderDefineExcludes_.Empty())
            return pixelShaderDefines_;

        std::vector<String> psDefines = pixelShaderDefines_.Split(' ');
        std::vector<String> psExcludes = pixelShaderDefineExcludes_.Split(' ');
        for (unsigned i = 0; i < psExcludes.size(); ++i)
            psDefines.erase(psExcludes.begin() + i);

        return String::Joined(psDefines, " ");
    }

    std::vector<SharedPtr<ShaderVariation> >& Pass::GetVertexShaders(const StringHash& extraDefinesHash)
    {
        // If empty hash, return the base shaders
        if (!extraDefinesHash.Value())
            return vertexShaders_;

        return extraVertexShaders_[extraDefinesHash];
    }

    std::vector<SharedPtr<ShaderVariation> >& Pass::GetPixelShaders(const StringHash& extraDefinesHash)
    {
        if (!extraDefinesHash.Value())
            return pixelShaders_;

        return extraPixelShaders_[extraDefinesHash];
    }

    unsigned Technique::basePassIndex = 0;
    unsigned Technique::alphaPassIndex = 0;
    unsigned Technique::materialPassIndex = 0;
    unsigned Technique::deferredPassIndex = 0;
    unsigned Technique::lightPassIndex = 0;
    unsigned Technique::litBasePassIndex = 0;
    unsigned Technique::litAlphaPassIndex = 0;
    unsigned Technique::shadowPassIndex = 0;

    std::unordered_map<String, uint32_t> Technique::passIndices;

    Technique::Technique(Context* context) :
        Resource(context),
        isDesktop_(false)
    {
#ifdef DESKTOP_GRAPHICS
        desktopSupport_ = true;
#else
        desktopSupport_ = false;
#endif
    }

    Technique::~Technique() = default;

    void Technique::RegisterObject(Context* context)
    {
        context->RegisterFactory<Technique>();
    }

    bool Technique::BeginLoad(Deserializer& source)
    {
        passes.clear();
        cloneTechniques.clear();

        SetMemoryUse(sizeof(Technique));

        SharedPtr<XMLFile> xml(new XMLFile(context_));
        if (!xml->Load(source))
            return false;

        XMLElement rootElem = xml->GetRoot();
        if (rootElem.HasAttribute("desktop"))
            isDesktop_ = rootElem.GetBool("desktop");

        String globalVS = rootElem.GetAttribute("vs");
        String globalPS = rootElem.GetAttribute("ps");
        String globalVSDefines = rootElem.GetAttribute("vsdefines");
        String globalPSDefines = rootElem.GetAttribute("psdefines");
        // End with space so that the pass-specific defines can be appended
        if (!globalVSDefines.Empty())
            globalVSDefines += ' ';
        if (!globalPSDefines.Empty())
            globalPSDefines += ' ';

        XMLElement passElem = rootElem.GetChild("pass");
        while (passElem)
        {
            if (passElem.HasAttribute("name"))
            {
                Pass* newPass = CreatePass(passElem.GetAttribute("name"));

                if (passElem.HasAttribute("desktop"))
                    newPass->SetIsDesktop(passElem.GetBool("desktop"));

                // Append global defines only when pass does not redefine the shader
                if (passElem.HasAttribute("vs"))
                {
                    newPass->SetVertexShader(passElem.GetAttribute("vs"));
                    newPass->SetVertexShaderDefines(passElem.GetAttribute("vsdefines"));
                }
                else
                {
                    newPass->SetVertexShader(globalVS);
                    newPass->SetVertexShaderDefines(globalVSDefines + passElem.GetAttribute("vsdefines"));
                }
                if (passElem.HasAttribute("ps"))
                {
                    newPass->SetPixelShader(passElem.GetAttribute("ps"));
                    newPass->SetPixelShaderDefines(passElem.GetAttribute("psdefines"));
                }
                else
                {
                    newPass->SetPixelShader(globalPS);
                    newPass->SetPixelShaderDefines(globalPSDefines + passElem.GetAttribute("psdefines"));
                }

                newPass->SetVertexShaderDefineExcludes(passElem.GetAttribute("vsexcludes"));
                newPass->SetPixelShaderDefineExcludes(passElem.GetAttribute("psexcludes"));

                if (passElem.HasAttribute("lighting"))
                {
                    String lighting = passElem.GetAttributeLower("lighting");
                    newPass->SetLightingMode((PassLightingMode)GetStringListIndex(lighting.CString(), lightingModeNames, (uint32_t)PassLightingMode::Unlit));
                }

                if (passElem.HasAttribute("blend"))
                {
                    String blend = passElem.GetAttributeLower("blend");
                    newPass->SetBlendMode((BlendMode)GetStringListIndex(blend.CString(), blendModeNames, BLEND_REPLACE));
                }

                if (passElem.HasAttribute("cull"))
                {
                    String cull = passElem.GetAttributeLower("cull");
                    newPass->SetCullMode((CullMode)GetStringListIndex(cull.CString(), cullModeNames, (uint32_t)CullMode::Count));
                }

                if (passElem.HasAttribute("depthtest"))
                {
                    String depthTest = passElem.GetAttributeLower("depthtest");
                    if (depthTest == "false")
                        newPass->SetDepthTestMode(CMP_ALWAYS);
                    else
                        newPass->SetDepthTestMode((CompareMode)GetStringListIndex(depthTest.CString(), compareModeNames, CMP_LESS));
                }

                if (passElem.HasAttribute("depthwrite"))
                    newPass->SetDepthWrite(passElem.GetBool("depthwrite"));

                if (passElem.HasAttribute("alphatocoverage"))
                    newPass->SetAlphaToCoverage(passElem.GetBool("alphatocoverage"));
            }
            else
                URHO3D_LOGERROR("Missing pass name");

            passElem = passElem.GetNext("pass");
        }

        return true;
    }

    void Technique::SetIsDesktop(bool enable)
    {
        isDesktop_ = enable;
    }

    void Technique::ReleaseShaders()
    {
        for (std::vector<SharedPtr<Pass> >::const_iterator i = passes.begin(); i != passes.end(); ++i)
        {
            Pass* pass = i->Get();
            if (pass)
                pass->ReleaseShaders();
        }
    }

    SharedPtr<Technique> Technique::Clone(const String& cloneName) const
    {
        SharedPtr<Technique> ret(new Technique(context_));
        ret->SetIsDesktop(isDesktop_);
        ret->SetName(cloneName);

        // Deep copy passes
        for (std::vector<SharedPtr<Pass> >::const_iterator i = passes.begin(); i != passes.end(); ++i)
        {
            Pass* srcPass = i->Get();
            if (!srcPass)
                continue;

            Pass* newPass = ret->CreatePass(srcPass->GetName());
            newPass->SetCullMode(srcPass->GetCullMode());
            newPass->SetBlendMode(srcPass->GetBlendMode());
            newPass->SetDepthTestMode(srcPass->GetDepthTestMode());
            newPass->SetLightingMode(srcPass->GetLightingMode());
            newPass->SetDepthWrite(srcPass->GetDepthWrite());
            newPass->SetAlphaToCoverage(srcPass->GetAlphaToCoverage());
            newPass->SetIsDesktop(srcPass->IsDesktop());
            newPass->SetVertexShader(srcPass->GetVertexShader());
            newPass->SetPixelShader(srcPass->GetPixelShader());
            newPass->SetVertexShaderDefines(srcPass->GetVertexShaderDefines());
            newPass->SetPixelShaderDefines(srcPass->GetPixelShaderDefines());
            newPass->SetVertexShaderDefineExcludes(srcPass->GetVertexShaderDefineExcludes());
            newPass->SetPixelShaderDefineExcludes(srcPass->GetPixelShaderDefineExcludes());
        }

        return ret;
    }

    Pass* Technique::CreatePass(const String& name)
    {
        Pass* oldPass = GetPass(name);
        if (oldPass)
            return oldPass;

        SharedPtr<Pass> newPass(new Pass(name));
        uint32_t passIndex = newPass->GetIndex();
        if (passIndex >= passes.size())
        {
            passes.resize(passIndex + 1);
        }
        passes[passIndex] = newPass;

        // Calculate memory use now
        SetMemoryUse((unsigned)(sizeof(Technique) + GetNumPasses() * sizeof(Pass)));

        return newPass;
    }

    void Technique::RemovePass(const String& name)
    {
        auto it = passIndices.find(name.ToLower());
        if (it == passIndices.end())
        {
            return;
        }
        else if (it->second < passes.size() && passes[it->second].Get())
        {
            passes[it->second].Reset();
            SetMemoryUse((unsigned)(sizeof(Technique) + GetNumPasses() * sizeof(Pass)));
        }
    }

    bool Technique::HasPass(const String& name) const
    {
        auto i = passIndices.find(name.ToLower());
        return i != passIndices.end() ? HasPass(i->second) : false;
    }

    Pass* Technique::GetPass(const String& name) const
    {
        auto i = passIndices.find(name.ToLower());
        return i != passIndices.end() ? GetPass(i->second) : nullptr;
    }

    Pass* Technique::GetSupportedPass(const String& name) const
    {
        auto i = passIndices.find(name.ToLower());
        return i != passIndices.end() ? GetSupportedPass(i->second) : nullptr;
    }

    uint32_t Technique::GetNumPasses() const
    {
        uint32_t ret = 0;

        for (std::vector<SharedPtr<Pass> >::const_iterator i = passes.begin(); i != passes.end(); ++i)
        {
            if (i->Get())
                ++ret;
        }

        return ret;
    }

    std::vector<String> Technique::GetPassNames() const
    {
        std::vector<String> ret;

        for (std::vector<SharedPtr<Pass> >::const_iterator i = passes.begin(); i != passes.end(); ++i)
        {
            Pass* pass = i->Get();
            if (pass)
                ret.push_back(pass->GetName());
        }

        return ret;
    }

    SharedPtr<Technique> Technique::CloneWithDefines(const String& vsDefines, const String& psDefines)
    {
        // Return self if no actual defines
        if (vsDefines.Empty() && psDefines.Empty())
            return SharedPtr<Technique>(this);

        std::pair<StringHash, StringHash> key = std::make_pair(StringHash(vsDefines), StringHash(psDefines));

        // Return existing if possible
        auto i = cloneTechniques.find(key);
        if (i != cloneTechniques.end())
            return i->second;

        // Set same name as the original for the clones to ensure proper serialization of the material. This should not be a problem
        // since the clones are never stored to the resource cache
        i = cloneTechniques.insert(std::make_pair(key, Clone(GetName()))).first;

        for (std::vector<SharedPtr<Pass> >::const_iterator j = i->second->passes.begin(); j != i->second->passes.end(); ++j)
        {
            Pass* pass = (*j);
            if (!pass)
                continue;

            if (!vsDefines.Empty())
                pass->SetVertexShaderDefines(pass->GetVertexShaderDefines() + " " + vsDefines);
            if (!psDefines.Empty())
                pass->SetPixelShaderDefines(pass->GetPixelShaderDefines() + " " + psDefines);
        }

        return i->second;
    }

    uint32_t Technique::GetPassIndex(const String& passName)
    {
        // Initialize built-in pass indices on first call
        if (passIndices.empty())
        {
            basePassIndex = passIndices["base"] = 0;
            alphaPassIndex = passIndices["alpha"] = 1;
            materialPassIndex = passIndices["material"] = 2;
            deferredPassIndex = passIndices["deferred"] = 3;
            lightPassIndex = passIndices["light"] = 4;
            litBasePassIndex = passIndices["litbase"] = 5;
            litAlphaPassIndex = passIndices["litalpha"] = 6;
            shadowPassIndex = passIndices["shadow"] = 7;
        }

        String nameLower = passName.ToLower();
        auto it = passIndices.find(nameLower);
        if (it != passIndices.end())
        {
            return it->second;
        }

        uint32_t newPassIndex = static_cast<uint32_t>(passIndices.size());
        passIndices[nameLower] = newPassIndex;
        return newPassIndex;
    }
}
