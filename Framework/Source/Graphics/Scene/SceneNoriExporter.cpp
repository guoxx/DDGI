/***************************************************************************
# Copyright (c) 2015, NVIDIA CORPORATION. All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
#  * Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
#  * Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
#  * Neither the name of NVIDIA CORPORATION nor the names of its
#    contributors may be used to endorse or promote products derived
#    from this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS ``AS IS'' AND ANY
# EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
# PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
# CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
# EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
# PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
# PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
# OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
***************************************************************************/
#include "Framework.h"
#include "SceneNoriExporter.h"
#include <fstream>
#include "Utils/Platform/OS.h"
#include "Graphics/Scene/Editor/SceneEditor.h"

#define SCENE_EXPORTER
#include "SceneExportImportCommon.h"

#include "pugixml/pugixml.hpp"

namespace Falcor
{
    FileDialogFilterVec SceneNoriExporter::kFileExtensionFilters = { {"xml", "Nori Scene Files"} };

    bool SceneNoriExporter::saveScene(const std::string& filename, const Scene* pScene, vec2 viewportSize)
    {
        SceneNoriExporter exporter(filename, pScene, viewportSize);
        return exporter.save();
    }

    static const char* getMaterialLayerNDF(uint32_t ndf)
    {
        return "beckmann";
        //switch (ndf)
        //{
        //case NDFBeckmann:
        //    return "beckmann";
        //case NDFGGX:
        //    return "ggx";
        //case NDFUser:
        //default:
        //    should_not_get_here();
        //    return "";
        //}
    }


    template <typename T>
    pugi::xml_attribute setNodeAttr(pugi::xml_node& node, const char* attrName, const T value)
    {
        pugi::xml_attribute attr = node.append_attribute(attrName);
        attr.set_value(value);
        return attr;
    }

    template <>
    pugi::xml_attribute setNodeAttr<std::string>(pugi::xml_node& node, const char* attrName, const std::string value)
    {
        pugi::xml_attribute attr = node.append_attribute(attrName);
        attr.set_value(value.c_str());
        return attr;
    }

    pugi::xml_node addNodeWithType(pugi::xml_node& parent, const std::string type)
    {
        pugi::xml_node comments = parent.append_child(pugi::xml_node_type::node_element);
        comments.set_name(type.c_str());
        return comments;
    }

    pugi::xml_node addComments(pugi::xml_node& parent, const std::string text)
    {
        pugi::xml_node comments = parent.append_child(pugi::xml_node_type::node_comment);
        comments.set_value(text.c_str());
        return comments;
    }

    pugi::xml_node addBoolean(pugi::xml_node& parent, const std::string name, bool b)
    {
        pugi::xml_node string = addNodeWithType(parent, "boolean");
        setNodeAttr(string, "name", name);
        setNodeAttr(string, "value", b ? "true" : "false");
        return string;
    }

    pugi::xml_node addFloat(pugi::xml_node& parent, const std::string name, float v)
    {
        pugi::xml_node nd = addNodeWithType(parent, "float");
        setNodeAttr(nd, "name", name);
        setNodeAttr(nd, "value", v);
        return nd;
    }

    pugi::xml_node addInteger(pugi::xml_node& parent, const std::string name, int32_t v)
    {
        pugi::xml_node integer = addNodeWithType(parent, "integer");
        setNodeAttr(integer, "name", name);
        setNodeAttr(integer, "value", v);
        return integer;
    }

    pugi::xml_node addString(pugi::xml_node& parent, const std::string name, const std::string str)
    {
        pugi::xml_node string = addNodeWithType(parent, "string");
        setNodeAttr(string, "name", name);
        setNodeAttr(string, "value", str);
        return string;
    }

    pugi::xml_node addSpectrum(pugi::xml_node& parent, const std::string name, const glm::vec3& rgb)
    {
        //pugi::xml_node spectrum = addNodeWithType(parent, "spectrum");
        pugi::xml_node spectrum = addNodeWithType(parent, "color");
        setNodeAttr(spectrum, "name", name);
        setNodeAttr(spectrum, "value",
            std::to_string(rgb.x) + ", " + std::to_string(rgb.y) + ", " + std::to_string(rgb.z));
        return spectrum;
    }

    pugi::xml_node addSpectrum(pugi::xml_node& parent, const std::string name, const float v)
    {
        //pugi::xml_node spectrum = addNodeWithType(parent, "spectrum");
        pugi::xml_node spectrum = addNodeWithType(parent, "color");
        setNodeAttr(spectrum, "name", name);
        setNodeAttr(spectrum, "value", std::to_string(v));
        return spectrum;
    }

    pugi::xml_node addPoint(pugi::xml_node& parent, const std::string name, const glm::vec3& pos)
    {
        pugi::xml_node point = addNodeWithType(parent, "point");
        setNodeAttr(point, "name", name);
        setNodeAttr(point, "x", pos.x);
        setNodeAttr(point, "y", pos.y);
        setNodeAttr(point, "z", pos.z);
        return point;
    }

    pugi::xml_node addVector(pugi::xml_node& parent, const std::string name, const glm::vec3& vec)
    {
        pugi::xml_node vector = addNodeWithType(parent, "vector");
        setNodeAttr(vector, "name", name);
#if 1
        setNodeAttr(vector, "value",
            std::to_string(vec.x) + ", " + std::to_string(vec.y) + ", " + std::to_string(vec.z));
#else
        setNodeAttr(vector, "x", vec.x);
        setNodeAttr(vector, "y", vec.y);
        setNodeAttr(vector, "z", vec.z);
#endif
        return vector;
    }

    pugi::xml_node addTransform(pugi::xml_node& parent,
        const std::string name,
        const glm::vec3& origin,
        const glm::vec3& target,
        const glm::vec3& up)
    {
        pugi::xml_node transform = addNodeWithType(parent, "transform");
        setNodeAttr(transform, "name", name);

        pugi::xml_node lookat = addNodeWithType(transform, "lookat");
        setNodeAttr(lookat, "origin",
            std::to_string(origin.x) + ", " + std::to_string(origin.y) + ", " + std::to_string(origin.z));
        setNodeAttr(lookat, "target",
            std::to_string(target.x) + ", " + std::to_string(target.y) + ", " + std::to_string(target.z));
        setNodeAttr(lookat, "up", std::to_string(up.x) + ", " + std::to_string(up.y) + ", " + std::to_string(up.z));

        return transform;
    }

    pugi::xml_node addTransformWithMatrix(pugi::xml_node& parent,
        const std::string name,
        const glm::mat4x4& transformMatrix)
    {
        pugi::xml_node transform = addNodeWithType(parent, "transform");
        setNodeAttr(transform, "name", name);

        pugi::xml_node matrix = addNodeWithType(transform, "matrix");

        std::string valueStr;
        for (int i = 0; i < 4; ++i)
        {
            for (int j = 0; j < 4; ++j)
            {
                valueStr += std::to_string(transformMatrix[j][i]) + ", ";
            }
        }
        setNodeAttr(matrix, "value", valueStr.substr(0, valueStr.length() - 2));

        return transform;
    }

    pugi::xml_node addScene(pugi::xml_node& parent)
    {
        pugi::xml_node scene = addNodeWithType(parent, "scene");
        //setNodeAttr(scene, "version", "0.6.0");
        return scene;
    }

    pugi::xml_node addIntegrator(pugi::xml_node& parent)
    {
        pugi::xml_node ingetrator = addNodeWithType(parent, "integrator");
        //setNodeAttr(ingetrator, "id", "integrator");
        setNodeAttr(ingetrator, "type", "path");
        addInteger(ingetrator, "maxDepth", 1);
        return ingetrator;
    }

    pugi::xml_node addBitmapTexture(pugi::xml_node& parent, const std::string name,
        const Texture::SharedPtr& pTexture, Sampler::AddressMode addressMode)
    {
        pugi::xml_node texture = addNodeWithType(parent, "texture");
        setNodeAttr(texture, "type", "bitmap");
        if (name.length())
        {
            setNodeAttr(texture, "name", name);
        }

        addString(texture, "filename", pTexture->getSourceFilename());
        if (addressMode == Sampler::AddressMode::Clamp)
        {
            addString(texture, "warpMode", "clamp");
        }
        else if (addressMode == Sampler::AddressMode::Wrap)
        {
            addString(texture, "warpMode", "repeat");
        }
        else
        {
            should_not_get_here();
        }
        if (isSrgbFormat(pTexture->getFormat()))
        {
            addFloat(texture, "gamma", 2.2f);
        }
        else
        {
            addFloat(texture, "gamma", 1.0f);
        }

        return texture;
    }

    pugi::xml_node addSampler(pugi::xml_node& parent, int32_t sampleCount)
    {
        pugi::xml_node sampler = addNodeWithType(parent, "sampler");
        setNodeAttr(sampler, "type", "independent");
        addInteger(sampler, "sampleCount", sampleCount);
        return sampler;
    }

    pugi::xml_node addFilm(pugi::xml_node& parent, int32_t width, int32_t height)
    {
#if 1
        addInteger(parent, "width", width);
        addInteger(parent, "height", height);
        return parent;
#else
        pugi::xml_node sampler = addNodeWithType(parent, "film");
        setNodeAttr(sampler, "type", "hdrfilm");
        addInteger(sampler, "width", width);
        addInteger(sampler, "height", height);
        return sampler;
#endif
    }

    bool SceneNoriExporter::save()
    {
		pugi::xml_document rootXmlDoc;
		pugi::xml_node sceneNode;

        rootXmlDoc.reset();

        addComments(rootXmlDoc, "\nAutomatic exported from Falcor\n");
        sceneNode = addScene(rootXmlDoc);

        addIntegrator(sceneNode);

        // Write everything else
        writeModels(sceneNode);
        writeLights(sceneNode);
        writeCameras(sceneNode);
        addSampler(sceneNode, mSampleCount);

        rootXmlDoc.save_file(mFilename.c_str(), PUGIXML_TEXT("    "), pugi::format_default, pugi::xml_encoding::encoding_utf8);
        return true;
    }

//    pugi::xml_node addCoatingLayer(const Material::Layer& layer, pugi::xml_node& parent)
//    {
//        assert(layer.type == Material::Layer::Type::Dielectric);
//        assert(layer.blend == Material::Layer::Blend::Fresnel);
//
//        if (layer.roughness.x == 0.0f)
//        {
//            pugi::xml_node coating = addNodeWithType(parent, "bsdf");
//            setNodeAttr(coating, "type", "coating");
//
//            addFloat(coating, "intIOR", layer.extraParam.x);
//
//            if (layer.pTexture != nullptr)
//            {
//                pugi::xml_node specularReflectance = addNodeWithType(coating, "texture");
//                setNodeAttr(specularReflectance, "type", "bitmap");
//                setNodeAttr(specularReflectance, "name", "specularReflectance");
//                addString(specularReflectance, "filename", layer.pTexture->getAbsoluteSourceFilename());
//                addFloat(specularReflectance, "gamma", -1);
//            }
//            else
//            {
//                addSpectrum(coating, "specularReflectance", layer.albedo);
//            }
//            return coating;
//        }
//        else
//        {
//            pugi::xml_node roughcoating = addNodeWithType(parent, "bsdf");
//            setNodeAttr(roughcoating, "type", "roughcoating");
//
//            addString(roughcoating, "distribution", getMaterialLayerNDF((uint32_t)layer.ndf));
//
//            addFloat(roughcoating, "intIOR", layer.extraParam.x);
//
//            if (layer.pTexture != nullptr)
//            {
//#if 0
//                pugi::xml_node alpha = addNodeWithType(roughcoating, "texture");
//                setNodeAttr(alpha, "type", "bitmap");
//                setNodeAttr(alpha, "name", "alpha");
//                addString(alpha, "filename", layer.pTexture->getAbsoluteSourceFilename());
//                addFloat(alpha, "gamma", -1);
//                addString(alpha, "channel", "a");
//#else
//                addFloat(roughcoating, "alpha", layer.roughness.x);
//#endif
//
//                pugi::xml_node specularReflectance = addNodeWithType(roughcoating, "texture");
//                setNodeAttr(specularReflectance, "type", "bitmap");
//                setNodeAttr(specularReflectance, "name", "specularReflectance");
//                addString(specularReflectance, "filename", layer.pTexture->getAbsoluteSourceFilename());
//                addFloat(specularReflectance, "gamma", -1);
//            }
//            else
//            {
//                addFloat(roughcoating, "alpha", layer.roughness.x);
//                addSpectrum(roughcoating, "specularReflectance", layer.albedo);
//            }
//            return roughcoating;
//        }
//    }
//
//    pugi::xml_node addSingleLayer(const Material::Layer& layer, pugi::xml_node& parent)
//    {
//        switch (layer.type)
//        {
//        case Material::Layer::Type::Lambert:
//        {
//            pugi::xml_node diffuse = addNodeWithType(parent, "bsdf");
//            setNodeAttr(diffuse, "type", "diffuse");
//            if (layer.pTexture != nullptr)
//            {
//                pugi::xml_node texture = addNodeWithType(diffuse, "texture");
//                setNodeAttr(texture, "type", "bitmap");
//                setNodeAttr(texture, "name", "reflectance");
//                addString(texture, "filename", layer.pTexture->getAbsoluteSourceFilename());
//                addFloat(texture, "gamma", -1);
//            }
//            else
//            {
//                pugi::xml_node spectrunm = addSpectrum(diffuse, "reflectance", layer.albedo);
//            }
//            return diffuse;
//        }
//        case Material::Layer::Type::Conductor:
//        {
//            if (layer.roughness.x == 0.0f)
//            {
//                pugi::xml_node conductor = addNodeWithType(parent, "bsdf");
//                setNodeAttr(conductor, "type", "conductor");
//
//                addSpectrum(conductor, "eta", layer.extraParam.x);
//                addSpectrum(conductor, "k", layer.extraParam.y);
//
//                if (layer.pTexture != nullptr)
//                {
//                    pugi::xml_node specularReflectance = addNodeWithType(conductor, "texture");
//                    setNodeAttr(specularReflectance, "type", "bitmap");
//                    setNodeAttr(specularReflectance, "name", "specularReflectance");
//                    addString(specularReflectance, "filename", layer.pTexture->getAbsoluteSourceFilename());
//                    addFloat(specularReflectance, "gamma", -1);
//                }
//                else
//                {
//                    pugi::xml_node spectrunm = addSpectrum(conductor, "specularReflectance", layer.albedo);
//                }
//                return conductor;
//            }
//            else
//            {
//                pugi::xml_node roughconductor = addNodeWithType(parent, "bsdf");
//                setNodeAttr(roughconductor, "type", "roughconductor");
//
//                addString(roughconductor, "distribution", getMaterialLayerNDF((uint32_t)layer.ndf));
//
//                addSpectrum(roughconductor, "eta", layer.extraParam.x);
//                addSpectrum(roughconductor, "k", layer.extraParam.y);
//
//                if (layer.pTexture != nullptr)
//                {
//#if 0
//                    pugi::xml_node alpha = addNodeWithType(roughconductor, "texture");
//                    setNodeAttr(alpha, "type", "bitmap");
//                    setNodeAttr(alpha, "name", "alpha");
//                    addString(alpha, "filename", layer.pTexture->getAbsoluteSourceFilename());
//                    addFloat(alpha, "gamma", -1);
//                    addString(alpha, "channel", "a");
//#else
//                    addFloat(roughconductor, "alpha", layer.roughness.x);
//#endif
//
//                    pugi::xml_node specularReflectance = addNodeWithType(roughconductor, "texture");
//                    setNodeAttr(specularReflectance, "type", "bitmap");
//                    setNodeAttr(specularReflectance, "name", "specularReflectance");
//                    addString(specularReflectance, "filename", layer.pTexture->getAbsoluteSourceFilename());
//                    addFloat(specularReflectance, "gamma", -1);
//                }
//                else
//                {
//                    addFloat(roughconductor, "alpha", layer.roughness.x);
//                    addSpectrum(roughconductor, "specularReflectance", layer.albedo);
//                }
//                return roughconductor;
//            }
//        }
//        case Material::Layer::Type::Dielectric:
//        {
//            if (layer.roughness.x == 0.0f)
//            {
//                pugi::xml_node dielectric = addNodeWithType(parent, "bsdf");
//                setNodeAttr(dielectric, "type", "dielectric");
//
//                addFloat(dielectric, "intIOR", layer.extraParam.x);
//
//                if (layer.pTexture != nullptr)
//                {
//                    pugi::xml_node specularReflectance = addNodeWithType(dielectric, "texture");
//                    setNodeAttr(specularReflectance, "type", "bitmap");
//                    setNodeAttr(specularReflectance, "name", "specularReflectance");
//                    addString(specularReflectance, "filename", layer.pTexture->getAbsoluteSourceFilename());
//                    addFloat(specularReflectance, "gamma", -1);
//                }
//                else
//                {
//                    addSpectrum(dielectric, "specularReflectance", layer.albedo);
//                }
//                return dielectric;
//            }
//            else
//            {
//                pugi::xml_node roughdielectric = addNodeWithType(parent, "bsdf");
//                setNodeAttr(roughdielectric, "type", "roughdielectric");
//
//                addString(roughdielectric, "distribution", getMaterialLayerNDF((uint32_t)layer.ndf));
//
//                addFloat(roughdielectric, "intIOR", layer.extraParam.x);
//
//                if (layer.pTexture != nullptr)
//                {
//#if 0
//                    pugi::xml_node alpha = addNodeWithType(roughdielectric, "texture");
//                    setNodeAttr(alpha, "type", "bitmap");
//                    setNodeAttr(alpha, "name", "alpha");
//                    addString(alpha, "filename", layer.pTexture->getAbsoluteSourceFilename());
//                    addFloat(alpha, "gamma", -1);
//                    addString(alpha, "channel", "a");
//#else
//                    addFloat(roughdielectric, "alpha", layer.roughness.x);
//#endif
//
//                    pugi::xml_node specularReflectance = addNodeWithType(roughdielectric, "texture");
//                    setNodeAttr(specularReflectance, "type", "bitmap");
//                    setNodeAttr(specularReflectance, "name", "specularReflectance");
//                    addString(specularReflectance, "filename", layer.pTexture->getAbsoluteSourceFilename());
//                    addFloat(specularReflectance, "gamma", -1);
//                }
//                else
//                {
//                    addFloat(roughdielectric, "alpha", layer.roughness.x);
//                    addSpectrum(roughdielectric, "specularReflectance", layer.albedo);
//                }
//                return roughdielectric;
//            }
//        }
//
//        case Material::Layer::Type::Emissive:
//        case Material::Layer::Type::User:
//        default:
//        {
//            assert(false);
//            break;
//        }
//        }
//
//        return pugi::xml_node{};
//    }

    void addMaterial(const Material::SharedPtr& pMat, pugi::xml_node& parent, bool overwriteByName)
    {
        pugi::xml_node curParent = parent;

        if (pMat->getNormalMap())
        {
            pugi::xml_node normalmap = addNodeWithType(curParent, "bsdf");
            setNodeAttr(normalmap, "type", "normalmap");
            setNodeAttr(normalmap, "name", pMat->getName());

            addBitmapTexture(normalmap, "", pMat->getNormalMap(), pMat->getSampler()->getAddressModeU());

            curParent = normalmap;
        }

        pugi::xml_node pbr = addNodeWithType(curParent, "bsdf");
        setNodeAttr(pbr, "type", "pbr");
        setNodeAttr(pbr, "name", pMat->getName());

        if (pMat->getBaseColorTexture())
            addBitmapTexture(pbr, "baseColor", pMat->getBaseColorTexture(), pMat->getSampler()->getAddressModeU());
        else
            addSpectrum(pbr, "baseColor", pMat->getBaseColor());

        if (pMat->getSpecularTexture())
            addBitmapTexture(pbr, "specular", pMat->getSpecularTexture(), pMat->getSampler()->getAddressModeU());
        else
            addSpectrum(pbr, "specular", pMat->getSpecularParams());

        if (pMat->getEmissiveTexture())
            addBitmapTexture(pbr, "emissive", pMat->getEmissiveTexture(), pMat->getSampler()->getAddressModeU());
        else
            addSpectrum(pbr, "emissive", pMat->getEmissiveColor());
    }

    void exportMeshDataToOBJ(const Model* pModel, std::string& filename)
    {
        std::ofstream fs(filename);

        for (uint32_t meshIdx = 0; meshIdx < pModel->getMeshCount(); ++meshIdx)
        {
            const Mesh::SharedPtr& pMesh = pModel->getMesh(meshIdx);
            Vao::SharedPtr pVao = pMesh->getVao();

            if (pVao->getPrimitiveTopology() != Vao::Topology::TriangleList)
            {
                logError("obj exporter doesn't support topologies other than triangles.");
            }

            const uint32_t vertCnt = pMesh->getVertexCount();
            const uint32_t vbCnt = pVao->getVertexBuffersCount();
            for (uint32_t vbIdx = 0; vbIdx < vbCnt; ++vbIdx)
            {
                Buffer::SharedPtr pVB = pVao->getVertexBuffer(vbIdx);
                float* pVBData = (float*)pVB->map(Buffer::MapType::Read);

                const VertexBufferLayout* pLayout = pVao->getVertexLayout()->getBufferLayout(vbIdx).get();
                for (uint32_t elemIdx = 0; elemIdx < pLayout->getElementCount(); ++elemIdx)
                {
                    float* pData = reinterpret_cast<float*>((uint8_t*)pVBData + pLayout->getElementOffset(elemIdx));

                    if (pLayout->getElementName(elemIdx) == VERTEX_POSITION_NAME)
                    {
                        assert(pLayout->getElementFormat(elemIdx) == ResourceFormat::RGB32Float);
                        for (uint32_t vertIdx = 0; vertIdx < vertCnt; ++vertIdx)
                        {
                            fs << "v " << pData[0] << " " << pData[1] << " " << pData[2] << std::endl;
                            pData = reinterpret_cast<float*>((uint8_t*)pData + pLayout->getStride());
                        }
                    }
                    else if (pLayout->getElementName(elemIdx) == VERTEX_NORMAL_NAME)
                    {
                        assert(pLayout->getElementFormat(elemIdx) == ResourceFormat::RGB32Float);
                        for (uint32_t vertIdx = 0; vertIdx < vertCnt; ++vertIdx)
                        {
                            fs << "vn " << pData[0] << " " << pData[1] << " " << pData[2] << std::endl;
                            pData = reinterpret_cast<float*>((uint8_t*)pData + pLayout->getStride());
                        }
                    }
                    else if (pLayout->getElementName(elemIdx) == VERTEX_TEXCOORD_NAME)
                    {
                        assert(pLayout->getElementFormat(elemIdx) == ResourceFormat::RG32Float);
                        for (uint32_t vertIdx = 0; vertIdx < vertCnt; ++vertIdx)
                        {
                            fs << "vt " << pData[0] << " " << pData[1] << " " << pData[2] << std::endl;
                            pData = reinterpret_cast<float*>((uint8_t*)pData + pLayout->getStride());
                        }
                    }
                    else
                    {
                        logError("Vertex attribute not supported.");
                    }
                }

                fs << "# " << vertCnt << " vertices" << std::endl;

                pVB->unmap();
            }


            {
                Buffer::SharedPtr pIB = pVao->getIndexBuffer();
                assert(pVao->getIndexBufferFormat() == ResourceFormat::R32Uint);
                uint32_t* pData = (uint32_t*)pIB->map(Buffer::MapType::Read);
                for (uint32_t triangleIdx = 0; triangleIdx < pMesh->getPrimitiveCount(); ++triangleIdx)
                {
                    fs << "f "
                        << (pData[3 * triangleIdx + 0] + 1) << " "
                        << (pData[3 * triangleIdx + 1] + 1) << " "
                        << (pData[3 * triangleIdx + 2] + 1) << std::endl;
                }

                pIB->unmap();
            }
        }
    }

    void addWavefrontOBJ(const Scene* pScene, uint32_t modelID, pugi::xml_node& parent)
    {
        assert(pScene->getModelInstanceCount(modelID) > 0);

        const Model* pModel = pScene->getModel(modelID).get();

        std::string shapegroupId = pModel->getName();

        assert(parent.child(shapegroupId.c_str()).empty());

        for (uint32_t i = 0; i < pScene->getModelInstanceCount(modelID); i++)
        {
            auto& pInstance = pScene->getModelInstance(modelID, i);

            //pugi::xml_node obj = addNodeWithType(parent, "shape");
            pugi::xml_node obj = addNodeWithType(parent, "mesh");
            setNodeAttr(obj, "type", "assimp");

            addComments(obj, pInstance->getName());

            std::string filename = pModel->getFilename();
            addString(obj, "filename", filename);

            addTransformWithMatrix(obj, "toWorld", pInstance->getTransformMatrix());

            const bool overwriteByName = pInstance->getObject()->getMaterialCount() > 1;
            for (uint32_t meshId = 0; meshId < pInstance->getObject()->getMeshCount(); ++meshId)
            {
                const Material::SharedPtr pMat = pInstance->getObject()->getMesh(meshId)->getMaterial();
                addMaterial(pMat, obj, overwriteByName);
            }
        }
    }

    void SceneNoriExporter::writeModels(pugi::xml_node& parent)
    {
        if (mpScene->getModelCount() == 0)
        {
            return;
        }

        addComments(parent, "Models");

        for (uint32_t i = 0; i < mpScene->getModelCount(); i++)
        {
            addWavefrontOBJ(mpScene, i, parent);
        }
    }

    void addSpotLight(const PointLight* pLight, pugi::xml_node& parent)
    {
        pugi::xml_node pointLight = addNodeWithType(parent, "emitter");
        setNodeAttr(pointLight, "type", "spot");

        addComments(pointLight, pLight->getName());

        // TODO
        assert(false);

        //addVector(jsonLight, allocator, SceneKeys::kLightIntensity, pLight->getIntensity());
        //addVector(jsonLight, allocator, SceneKeys::kLightPos, pLight->getWorldPosition());
        //addVector(jsonLight, allocator, SceneKeys::kLightDirection, pLight->getWorldDirection());
        //addLiteral(jsonLight, allocator, SceneKeys::kLightOpeningAngle, glm::degrees(pLight->getOpeningAngle()));
        //addLiteral(jsonLight, allocator, SceneKeys::kLightPenumbraAngle, glm::degrees(pLight->getPenumbraAngle()));
    }

    void addPointLight(const PointLight* pLight, pugi::xml_node& parent)
    {
        assert(pLight->getOpeningAngle() >= (float)M_PI);

        pugi::xml_node pointLight = addNodeWithType(parent, "emitter");
        setNodeAttr(pointLight, "type", "point");

        addComments(pointLight, pLight->getName());
        addSpectrum(pointLight, "intensity", pLight->getIntensity());
        addPoint(pointLight, "position", pLight->getWorldPosition());
    }

    void addDirectionalLight(const DirectionalLight* pLight, pugi::xml_node& parent)
    {
        pugi::xml_node pointLight = addNodeWithType(parent, "emitter");
        setNodeAttr(pointLight, "type", "directional");

        addComments(pointLight, pLight->getName());
        addSpectrum(pointLight, "irradiance", pLight->getIntensity());
        addVector(pointLight, "direction", pLight->getWorldDirection());
    }

    void addPunctualLight(const Scene* pScene, uint32_t lightID, pugi::xml_node& parent)
    {
        const auto pLight = pScene->getLight(lightID);

        switch (pLight->getType())
        {
        case LightPoint:
        {
            PointLight* pl = (PointLight*)pLight.get();
            if (pl->getOpeningAngle() >= (float)M_PI)
            {
                addPointLight(pl, parent);
            }
            else
            {
                addSpotLight(pl, parent);
            }
            break;
        }
        case LightDirectional:
            addDirectionalLight((DirectionalLight*)pLight.get(), parent);
            break;
        default:
            should_not_get_here();
            break;
        }
    }

    void SceneNoriExporter::writeLights(pugi::xml_node& parent)
    {
        if (mpScene->getLightCount() == 0)
        {
            return;
        }

        addComments(parent, "Punctual light sources");

        for (uint32_t i = 0; i < mpScene->getLightCount(); i++)
        {
            if (mpScene->getLights()[i]->getType() != LightPoint &&
                mpScene->getLights()[i]->getType() != LightDirectional)
            {
                continue;
            }

            addPunctualLight(mpScene, i, parent);
        }
    }

    void addPerspectiveCamera(const Scene* pScene, const Camera* pCamera, pugi::xml_node& parent, vec2 viewportSize, int32_t sampleCount)
    {
        //pugi::xml_node sensor = addNodeWithType(parent, "sensor");
        pugi::xml_node sensor = addNodeWithType(parent, "camera");
        setNodeAttr(sensor, "type", "perspective");

        addComments(sensor, pCamera->getName());

        addTransform(sensor, "toWorld", pCamera->getPosition(), pCamera->getTarget(), pCamera->getUpVector());

#if 1
        const float fovY = pCamera->getFocalLength() == 0.0f
            ? 0.0f
            : focalLengthToFovY(pCamera->getFocalLength(), Camera::kDefaultFrameHeight);
#if 1
        const float fovX = std::atan(std::tan(fovY/2)*viewportSize.x/viewportSize.y)*2;
        addFloat(sensor, "fov", glm::degrees(fovX));
#else
        addFloat(sensor, "fov", glm::degrees(fovY));
        addString(sensor, "fovAxis", "y");
#endif
#else
        std::string focalLenStr = std::to_string(pCamera->getFocalLength()) + "mm";
        addString(sensor, "focalLength", focalLenStr.c_str());
#endif

        glm::vec2 depthRange;
        depthRange[0] = pCamera->getNearPlane();
        depthRange[1] = pCamera->getFarPlane();

        addFloat(sensor, "nearClip", pCamera->getNearPlane());
        addFloat(sensor, "farClip", pCamera->getFarPlane());

        //addLiteral(jsonCamera, allocator, SceneKeys::kCamAspectRatio, pCamera->getAspectRatio());

        addFilm(sensor, (int32_t)viewportSize.x, (int32_t)viewportSize.y);
    }

    void SceneNoriExporter::writeCameras(pugi::xml_node& parent)
    {
        if (mpScene->getCameraCount() == 0)
        {
            return;
        }

        addComments(parent, "Default Camera");

        const Camera* pCamera = mpScene->getActiveCamera().get();
        addPerspectiveCamera(mpScene, pCamera, parent, mViewportSize, mSampleCount);
    }
}
