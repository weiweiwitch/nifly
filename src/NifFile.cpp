/*
BodySlide and Outfit Studio
See the included LICENSE file
*/

#include "NifFile.hpp"
#include "NifUtil.hpp"

#include <fstream>
#include <regex>
#include <set>
#include <unordered_set>
#include <queue>

using namespace nifly;

template<class T>
T* NifFile::FindBlockByName(const std::string& name) {
	for (auto& block : blocks) {
		auto namedBlock = dynamic_cast<T*>(block.get());
		if (namedBlock && name == namedBlock->GetName())
			return namedBlock;
	}

	return nullptr;
}

int NifFile::GetBlockID(NiObject* block) {
	auto it = std::find_if(blocks.begin(), blocks.end(), [&block](const auto& ptr) {
		return ptr.get() == block;
	});

	if (it != blocks.end())
		return std::distance(blocks.begin(), it);

	return NIF_NPOS;
}

NiNode* NifFile::GetParentNode(NiObject* childBlock) {
	if (childBlock != nullptr) {
		int childId = GetBlockID(childBlock);
		for (auto& block : blocks) {
			auto node = dynamic_cast<NiNode*>(block.get());
			if (node) {
				auto children = node->GetChildren();
				for (auto it = children.begin(); it < children.end(); ++it) {
					if (childId == it->GetIndex())
						return node;
				}
			}
		}
	}

	return nullptr;
}

void NifFile::SetParentNode(NiObject* childBlock, NiNode* newParent) {
	if (!childBlock)
		return;

	if (!newParent) {
		newParent = GetRootNode();

		if (!newParent)
			return;
	}

	if (childBlock == newParent)
		return;

	int childId = GetBlockID(childBlock);
	for (auto& block : blocks) {
		auto node = dynamic_cast<NiNode*>(block.get());
		if (!node)
			continue;

		auto& children = node->GetChildren();
		for (int ci = 0; ci < children.GetSize(); ++ci) {
			if (childId != children.GetBlockRef(ci))
				continue;

			// We have now found the node's old parent
			if (newParent != node) {
				children.RemoveBlockRef(ci);
				newParent->GetChildren().AddBlockRef(childId);
			}

			return;
		}
	}

	// If we get here, the node's old parent was not found.
	newParent->GetChildren().AddBlockRef(childId);
}

std::vector<NiNode*> NifFile::GetNodes() {
	std::vector<NiNode*> outList;
	for (auto& block : blocks) {
		auto node = dynamic_cast<NiNode*>(block.get());
		if (node)
			outList.push_back(node);
	}

	return outList;
}

void NifFile::CopyFrom(const NifFile& other) {
	if (isValid)
		Clear();

	isValid = other.isValid;
	hasUnknown = other.hasUnknown;
	isTerrain = other.isTerrain;

	hdr = NiHeader(other.hdr);

	size_t nBlocks = other.blocks.size();
	blocks.resize(nBlocks);

	for (uint32_t i = 0; i < nBlocks; i++)
		blocks[i] = std::unique_ptr<NiObject>(other.blocks[i]->Clone());

	hdr.SetBlockReference(&blocks);
	LinkGeomData();
}

void NifFile::LinkGeomData() {
	for (auto& block : blocks) {
		auto geom = dynamic_cast<NiGeometry*>(block.get());
		if (geom) {
			auto geomData = hdr.GetBlock<NiGeometryData>(geom->GetDataRef());
			if (geomData)
				geom->SetGeomData(geomData);
		}
	}
}

void NifFile::RemoveInvalidTris() {
	for (auto& shape : GetShapes()) {
		std::vector<Triangle> tris;
		if (shape->GetTriangles(tris)) {
			uint16_t numVerts = shape->GetNumVertices();
			tris.erase(std::remove_if(tris.begin(),
									  tris.end(),
									  [&](auto& t) {
										  return t.p1 >= numVerts || t.p2 >= numVerts || t.p3 >= numVerts;
									  }),
					   tris.end());

			shape->SetTriangles(tris);
		}
	}
}

size_t NifFile::GetVertexLimit() {
	constexpr size_t maxVertIndex = std::numeric_limits<uint16_t>::max();
	return maxVertIndex;
}

size_t NifFile::GetTriangleLimit() {
	size_t maxTriIndex = std::numeric_limits<uint32_t>::max();
	if (hdr.GetVersion().User() >= 12 && hdr.GetVersion().Stream() < 130)
		maxTriIndex = std::numeric_limits<uint16_t>::max();

	return maxTriIndex;
}

void NifFile::Create(const NiVersion& version) {
	Clear();
	hdr.SetVersion(version);
	hdr.SetBlockReference(&blocks);

	auto rootNode = new NiNode();
	rootNode->SetName("Scene Root");
	hdr.AddBlock(rootNode);

	isValid = true;
}

void NifFile::Clear() {
	isValid = false;
	hasUnknown = false;
	isTerrain = false;

	blocks.clear();
	hdr.Clear();
}

int NifFile::Load(const std::filesystem::path& fileName, const NifLoadOptions& options) {
	std::fstream file(fileName, std::ios::in | std::ios::binary);
	return Load(file, options);
}

int NifFile::Load(std::iostream& file, const NifLoadOptions& options) {
	Clear();

	isTerrain = options.isTerrain;

	if (file) {
		NiStream stream(&file, &hdr.GetVersion());
		hdr.Get(stream);

		if (!hdr.IsValid()) {
			Clear();
			return 1;
		}

		NiVersion& version = stream.GetVersion();
		if (!(version.File() >= NiVersion::ToFile(20, 2, 0, 7)
			  && (version.User() == 11 || version.User() == 12))) {
			Clear();
			return 2;
		}

		uint32_t nBlocks = hdr.GetNumBlocks();
		blocks.resize(nBlocks);

		auto& nifactories = NiFactoryRegister::Get();
		for (uint32_t i = 0; i < nBlocks; i++) {
			std::string blockTypeStr = hdr.GetBlockTypeStringById(i);

			auto nifactory = nifactories.GetFactoryByName(blockTypeStr);
			if (nifactory) {
				blocks[i] = nifactory->Load(stream);
			}
			else {
				hasUnknown = true;
				blocks[i] = std::make_unique<NiUnknown>(stream, hdr.GetBlockSize(i));
			}
		}

		hdr.SetBlockReference(&blocks);
	}
	else {
		Clear();
		return 1;
	}

	PrepareData();
	isValid = true;
	return 0;
}

void NifFile::SetShapeOrder(const std::vector<std::string>& order) {
	if (hasUnknown)
		return;

	std::vector<int> delta;
	bool hadoffset = false;

	// Have to do this in multiple passes
	do {
		std::vector<std::string> oldOrder = GetShapeNames();
		std::vector<int> oldOrderIds;
		for (const auto& s : oldOrder) {
			int blockID = GetBlockID(FindBlockByName<NiShape>(s));
			if (blockID != NIF_NPOS)
				oldOrderIds.push_back(blockID);
		}

		if (order.size() != oldOrder.size())
			return;

		// Get movement offset for each item.  This is the difference between old and new position.
		delta.clear();
		delta.resize(order.size());

		for (size_t p = 0; p < oldOrder.size(); p++)
			delta[p] = (std::find(order.begin(), order.end(), oldOrder[p]) - order.begin()) - p;

		hadoffset = false;
		//Positive offsets mean that the item has moved down the list.  By necessity, that means another item has moved up the list.
		// thus, we only need to move the "rising" items, the other blocks will naturally end up in the right place.

		// find first negative delta, and raise it in list.  The first item can't have a negative delta
		for (size_t i = 1; i < delta.size(); i++) {
			// don't move positive or zero offset items.
			if (delta[i] >= 0)
				continue;

			hadoffset = true;
			int c = 0 - delta[i];
			int p = i;
			while (c > 0) {
				hdr.SwapBlocks(oldOrderIds[p], oldOrderIds[p - 1]);
				p--;
				c--;
			}
			break;
		}

	} while (hadoffset);
}

void NifFile::SetSortIndex(const int id, std::vector<int>& newIndices, int& newIndex) {
	// Assign new sort index
	if (newIndices.size() > id)
		newIndices[id] = ++newIndex;
}

void NifFile::SortAVObject(NiAVObject* avobj, std::vector<int>& newIndices, int& newIndex) {
	int id = NIF_NPOS;
	for (auto& r : avobj->GetExtraData()) {
		id = r.GetIndex();
		if (id != NIF_NPOS)
			SetSortIndex(id, newIndices, newIndex);
	}

	id = avobj->GetControllerRef();
	if (id != NIF_NPOS)
		SetSortIndex(id, newIndices, newIndex);

	for (auto& r : avobj->GetProperties()) {
		id = r.GetIndex();
		if (id != NIF_NPOS)
			SetSortIndex(id, newIndices, newIndex);

		auto shader = hdr.GetBlock<NiShader>(id);
		if (shader) {
			id = shader->GetTextureSetRef();
			if (id != NIF_NPOS)
				SetSortIndex(id, newIndices, newIndex);
		}
	}

	id = avobj->GetCollisionRef();
	if (id != NIF_NPOS)
		SetSortIndex(id, newIndices, newIndex);
}

void NifFile::SortShape(NiShape* shape, std::vector<int>& newIndices, int& newIndex) {
	int id = shape->GetDataRef();
	if (id != NIF_NPOS)
		SetSortIndex(id, newIndices, newIndex);

	id = shape->GetSkinInstanceRef();
	if (id != NIF_NPOS) {
		SetSortIndex(id, newIndices, newIndex);

		auto niSkinInst = hdr.GetBlock<NiSkinInstance>(id);
		if (niSkinInst) {
			id = niSkinInst->GetDataRef();
			if (id != NIF_NPOS)
				SetSortIndex(id, newIndices, newIndex);

			id = niSkinInst->GetSkinPartitionRef();
			if (id != NIF_NPOS)
				SetSortIndex(id, newIndices, newIndex);
		}

		auto bsSkinInst = hdr.GetBlock<BSSkinInstance>(id);
		if (bsSkinInst) {
			id = bsSkinInst->GetDataRef();
			if (id != NIF_NPOS)
				SetSortIndex(id, newIndices, newIndex);
		}
	}

	id = shape->GetShaderPropertyRef();
	if (id != NIF_NPOS) {
		SetSortIndex(id, newIndices, newIndex);

		auto shader = hdr.GetBlock<NiShader>(id);
		if (shader) {
			id = shader->GetTextureSetRef();
			if (id != NIF_NPOS)
				SetSortIndex(id, newIndices, newIndex);
		}
	}

	id = shape->GetAlphaPropertyRef();
	if (id != NIF_NPOS)
		SetSortIndex(id, newIndices, newIndex);
}

void NifFile::SortGraph(NiNode* root, std::vector<int>& newIndices, int& newIndex) {
	auto& children = root->GetChildren();
	std::vector<int> indices;
	children.GetIndices(indices);
	children.Clear();

	for (uint32_t i = 0; i < hdr.GetNumBlocks(); i++)
		if (std::find(indices.begin(), indices.end(), i) != indices.end())
			children.AddBlockRef(i);

	if (children.GetSize() > 0) {
		if (hdr.GetVersion().IsFO3()) {
			auto bookmark = children.begin();
			auto peek = children.begin();

			// For FO3, put shapes at start of children
			for (uint32_t i = 0; i < children.GetSize(); i++) {
				auto shape = hdr.GetBlock<NiShape>(children.GetBlockRef(i));
				if (shape) {
					std::iter_swap(bookmark, peek);
					if (i != 0)
						++bookmark;
				}
				++peek;
			}
		}
		else {
			auto bookmark = children.end() - 1;
			auto peek = children.end() - 1;

			// Put shapes at end of children
			for (int i = children.GetSize() - 1; i >= 0; i--) {
				auto shape = hdr.GetBlock<NiShape>(children.GetBlockRef(i));
				if (shape) {
					std::iter_swap(bookmark, peek);
					if (i != 0)
						--bookmark;
				}

				if (i != 0)
					--peek;
			}
		}

		auto bookmark = children.begin();
		auto peek = children.begin();

		if (hdr.GetVersion().IsFO3()) {
			// For FO3, put nodes at start of children if they have children
			for (uint32_t i = 0; i < children.GetSize(); i++) {
				auto node = hdr.GetBlock<NiNode>(children.GetBlockRef(i));
				if (node && node->GetChildren().GetSize() > 0) {
					std::iter_swap(bookmark, peek);
					++bookmark;
				}
				++peek;
			}
		}
		else {
			// Put nodes at start of children
			for (uint32_t i = 0; i < children.GetSize(); i++) {
				auto node = hdr.GetBlock<NiNode>(children.GetBlockRef(i));
				if (node) {
					std::iter_swap(bookmark, peek);
					++bookmark;
				}
				++peek;
			}
		}

		// Update children
		for (auto& child : children) {
			int oldChildId = child.GetIndex();
			if (oldChildId != NIF_NPOS) {
				// Store new index of block
				SetSortIndex(oldChildId, newIndices, newIndex);

				// Update NiAVObject children
				auto avobj = hdr.GetBlock<NiAVObject>(oldChildId);
				if (avobj)
					SortAVObject(avobj, newIndices, newIndex);

				// Recurse through all children
				auto node = hdr.GetBlock<NiNode>(oldChildId);
				if (node)
					SortGraph(node, newIndices, newIndex);

				// Update shape children
				auto shape = hdr.GetBlock<NiShape>(oldChildId);
				if (shape)
					SortShape(shape, newIndices, newIndex);
			}
		}

		// Update effect children
		for (auto& effect : root->GetEffects()) {
			auto avobj = hdr.GetBlock<NiAVObject>(effect.GetIndex());
			if (avobj)
				SortAVObject(avobj, newIndices, newIndex);
		}
	}
}

void NifFile::PrettySortBlocks() {
	if (hasUnknown)
		return;

	std::vector<int> newOrder(hdr.GetNumBlocks());
	for (size_t i = 0; i < newOrder.size(); i++)
		newOrder[i] = i;

	auto root = GetRootNode();
	if (root) {
		int newIndex = GetBlockID(root);
		SortAVObject(root, newOrder, newIndex);
		SortGraph(root, newOrder, newIndex);
	}

	hdr.SetBlockOrder(newOrder);

	std::vector<NiObject*> tree;
	GetTree(tree);

	hdr.FixBlockAlignment(tree);
}

bool NifFile::DeleteUnreferencedNodes(int* deletionCount) {
	if (hasUnknown)
		return false;

	auto root = GetRootNode();
	if (!root)
		return false;

	for (auto& node : GetNodes()) {
		if (node == root)
			continue;

		int blockId = GetBlockID(node);
		if (blockId == NIF_NPOS)
			continue;

		if (!CanDeleteNode(node))
			continue;

		if (hdr.GetBlockRefCount(blockId) < 2) {
			hdr.DeleteBlock(blockId);

			if (deletionCount)
				(*deletionCount)++;

			// Deleting a block can cause others to become unreferenced
			return DeleteUnreferencedNodes(deletionCount);
		}
	}

	return true;
}

NiNode* NifFile::AddNode(const std::string& nodeName, const MatTransform& xformToParent, NiNode* parent) {
	if (!parent)
		parent = GetRootNode();
	if (!parent)
		return nullptr;

	auto newNode = new NiNode();
	newNode->SetName(nodeName);
	newNode->SetTransformToParent(xformToParent);

	int newNodeId = hdr.AddBlock(newNode);
	if (newNodeId != NIF_NPOS)
		parent->GetChildren().AddBlockRef(newNodeId);

	return newNode;
}

void NifFile::DeleteNode(const std::string& nodeName) {
	hdr.DeleteBlock(GetBlockID(FindBlockByName<NiNode>(nodeName)));
}

bool NifFile::CanDeleteNode(NiNode* node) {
	if (!node)
		return false;

	std::set<Ref*> refs;
	node->GetChildRefs(refs);

	// Only delete if the node has no child refs
	for (auto& ref : refs)
		if (ref->GetIndex() != NIF_NPOS)
			return false;

	return true;
}

bool NifFile::CanDeleteNode(const std::string& nodeName) {
	auto node = FindBlockByName<NiNode>(nodeName);
	return CanDeleteNode(node);
}

std::string NifFile::GetNodeName(const int blockID) {
	std::string name;

	auto n = hdr.GetBlock<NiNode>(blockID);
	if (n) {
		name = n->GetName();
		if (name.empty())
			name = "_unnamed_";
	}

	return name;
}

void NifFile::SetNodeName(const int blockID, const std::string& newName) {
	auto node = hdr.GetBlock<NiNode>(blockID);
	if (!node)
		return;

	node->SetName(newName);
}

int NifFile::AssignExtraData(NiAVObject* target, NiExtraData* extraData) {
	int extraDataId = hdr.AddBlock(extraData);
	target->GetExtraData().AddBlockRef(extraDataId);
	return extraDataId;
}

NiShader* NifFile::GetShader(NiShape* shape) {
	auto shader = hdr.GetBlock<NiShader>(shape->GetShaderPropertyRef());
	if (shader)
		return shader;

	for (auto& prop : shape->GetProperties()) {
		auto shaderProp = hdr.GetBlock<NiShader>(prop.GetIndex());
		if (shaderProp)
			return shaderProp;
	}

	return nullptr;
}

NiMaterialProperty* NifFile::GetMaterialProperty(NiShape* shape) {
	for (auto& prop : shape->GetProperties()) {
		auto material = hdr.GetBlock<NiMaterialProperty>(prop.GetIndex());
		if (material)
			return material;
	}

	return nullptr;
}

NiStencilProperty* NifFile::GetStencilProperty(NiShape* shape) {
	for (auto& prop : shape->GetProperties()) {
		auto stencil = hdr.GetBlock<NiStencilProperty>(prop.GetIndex());
		if (stencil)
			return stencil;
	}

	return nullptr;
}

int NifFile::GetTextureSlot(NiShader* shader, std::string& outTexFile, int texIndex) {
	outTexFile.clear();

	int textureSetRef = shader->GetTextureSetRef();
	if (textureSetRef == NIF_NPOS) {
		auto effectShader = dynamic_cast<BSEffectShaderProperty*>(shader);
		if (effectShader) {
			switch (texIndex) {
				case 0: outTexFile = effectShader->sourceTexture.GetString(); break;
				case 1: outTexFile = effectShader->normalTexture.GetString(); break;
				case 3: outTexFile = effectShader->greyscaleTexture.GetString(); break;
				case 4: outTexFile = effectShader->envMapTexture.GetString(); break;
				case 5: outTexFile = effectShader->envMaskTexture.GetString(); break;
			}

			return 2;
		}

		return 0;
	}

	auto textureSet = hdr.GetBlock<BSShaderTextureSet>(textureSetRef);
	if (!textureSet || texIndex + 1 > textureSet->numTextures)
		return 0;

	outTexFile = textureSet->textures[texIndex].GetString();
	return 1;
}

void NifFile::SetTextureSlot(NiShader* shader, std::string& inTexFile, int texIndex) {
	int textureSetRef = shader->GetTextureSetRef();
	if (textureSetRef == NIF_NPOS) {
		auto effectShader = dynamic_cast<BSEffectShaderProperty*>(shader);
		if (effectShader) {
			switch (texIndex) {
				case 0: effectShader->sourceTexture.SetString(inTexFile); break;
				case 1: effectShader->normalTexture.SetString(inTexFile); break;
				case 3: effectShader->greyscaleTexture.SetString(inTexFile); break;
				case 4: effectShader->envMapTexture.SetString(inTexFile); break;
				case 5: effectShader->envMaskTexture.SetString(inTexFile); break;
			}
		}

		return;
	}

	auto textureSet = hdr.GetBlock<BSShaderTextureSet>(textureSetRef);
	if (!textureSet || texIndex + 1 > textureSet->numTextures)
		return;

	textureSet->textures[texIndex].SetString(inTexFile);
}

void NifFile::TrimTexturePaths() {
	auto fTrimPath = [&isTerrain = isTerrain](std::string& tex) -> std::string& {
		if (!tex.empty()) {
			tex = std::regex_replace(tex,
									 std::regex("/+|\\\\+"),
									 "\\"); // Replace multiple slashes or forward slashes with one backslash
			tex = std::regex_replace(tex,
									 std::regex(R"(^(.*?)\\textures\\)", std::regex_constants::icase),
									 ""); // Remove everything before the first occurence of "\textures\"
			tex = std::regex_replace(tex, std::regex("^\\\\+"), ""); // Remove all backslashes from the front
			tex = std::regex_replace(
				tex,
				std::regex("^(?!^textures\\\\)", std::regex_constants::icase),
				"textures\\"); // If the path doesn't start with "textures\", add it to the front

			if (isTerrain)
				tex = std::regex_replace(tex,
										 std::regex("^(?!^Data\\\\)", std::regex_constants::icase),
										 "Data\\"); // If the path doesn't start with "Data\", add it to the front
		}
		return tex;
	};

	for (auto& shape : GetShapes()) {
		NiShader* shader = GetShader(shape);
		if (shader) {
			auto textureSet = hdr.GetBlock<BSShaderTextureSet>(shader->GetTextureSetRef());
			if (textureSet) {
				for (int i = 0; i < textureSet->numTextures; i++) {
					std::string tex = textureSet->textures[i].GetString();
					textureSet->textures[i].SetString(fTrimPath(tex));
				}

				auto effectShader = dynamic_cast<BSEffectShaderProperty*>(shader);
				if (effectShader) {
					std::string tex = effectShader->sourceTexture.GetString();
					effectShader->sourceTexture.SetString(fTrimPath(tex));

					tex = effectShader->normalTexture.GetString();
					effectShader->normalTexture.SetString(fTrimPath(tex));

					tex = effectShader->greyscaleTexture.GetString();
					effectShader->greyscaleTexture.SetString(fTrimPath(tex));

					tex = effectShader->envMapTexture.GetString();
					effectShader->envMapTexture.SetString(fTrimPath(tex));

					tex = effectShader->envMaskTexture.GetString();
					effectShader->envMaskTexture.SetString(fTrimPath(tex));
				}
			}
		}
	}
}

void NifFile::CloneChildren(NiObject* block, NifFile* srcNif) {
	if (!srcNif)
		srcNif = this;

	// Assign new refs and strings, rebind ptrs where possible
	std::function<void(NiObject*, int, int)> cloneBlock =
		[&](NiObject* b, int parentOldId, int parentNewId) -> void {
		std::set<Ref*> refs;
		b->GetChildRefs(refs);

		for (auto& r : refs) {
			int srcId = r->GetIndex();
			auto srcChild = srcNif->hdr.GetBlock<NiObject>(srcId);
			if (srcChild) {
				auto destChild = srcChild->Clone();
				int destId = hdr.AddBlock(destChild);
				r->SetIndex(destId);

				std::set<StringRef*> strRefs;
				destChild->GetStringRefs(strRefs);

				for (auto& str : strRefs) {
					int strId = hdr.AddOrFindStringId(str->GetString());
					str->SetIndex(strId);
				}

				if (parentOldId != NIF_NPOS) {
					std::set<Ref*> ptrs;
					destChild->GetPtrs(ptrs);

					for (auto& p : ptrs)
						if (p->GetIndex() == parentOldId)
							p->SetIndex(parentNewId);

					cloneBlock(destChild, parentOldId, parentNewId);
				}
				else
					cloneBlock(destChild, srcId, destId);
			}
		}
	};

	cloneBlock(block, NIF_NPOS, NIF_NPOS);
}

NiShape* NifFile::CloneShape(NiShape* srcShape, const std::string& destShapeName, NifFile* srcNif) {
	if (!srcNif)
		srcNif = this;

	if (!srcShape)
		return nullptr;

	auto rootNode = GetRootNode();
	auto srcRootNode = srcNif->GetRootNode();

	// Geometry
	auto destShape = static_cast<NiShape*>(srcShape->Clone());
	destShape->SetName(destShapeName);

	int destId = hdr.AddBlock(destShape);
	if (srcNif == this) {
		// Assign copied geometry to the same parent
		auto parentNode = GetParentNode(srcShape);
		if (parentNode)
			parentNode->GetChildren().AddBlockRef(destId);
	}
	else if (rootNode)
		rootNode->GetChildren().AddBlockRef(destId);

	// Children
	CloneChildren(destShape, srcNif);

	// Geometry Data
	auto destGeomData = hdr.GetBlock<NiTriBasedGeomData>(destShape->GetDataRef());
	if (destGeomData)
		destShape->SetGeomData(destGeomData);

	// Shader
	auto destShader = GetShader(destShape);
	if (destShader) {
		if (hdr.GetVersion().IsSK() || hdr.GetVersion().IsSSE()) {
			// Kill normals and tangents
			if (destShader->IsModelSpace()) {
				destShape->SetNormals(false);
				destShape->SetTangents(false);
			}
		}
	}

	// Bones
	std::vector<std::string> srcBoneList;
	srcNif->GetShapeBoneList(srcShape, srcBoneList);

	auto destBoneCont = hdr.GetBlock<NiBoneContainer>(destShape->GetSkinInstanceRef());
	if (destBoneCont)
		destBoneCont->GetBones().Clear();

	if (rootNode && srcRootNode) {
		std::function<void(NiNode*)> cloneNodes = [&](NiNode* srcNode) -> void {
			std::string boneName = srcNode->GetName();

			// Insert as root child by default
			NiNode* nodeParent = rootNode;

			// Look for existing node to use as parent instead
			auto srcNodeParent = srcNif->GetParentNode(srcNode);
			if (srcNodeParent) {
				auto parent = FindBlockByName<NiNode>(srcNodeParent->GetName());
				if (parent)
					nodeParent = parent;
			}

			auto node = FindBlockByName<NiNode>(boneName);
			int boneID = GetBlockID(node);
			if (!node) {
				// Clone missing node into the right parent
				boneID = CloneNamedNode(boneName, srcNif);
				nodeParent->GetChildren().AddBlockRef(boneID);
			}
			else {
				// Move existing node to non-root parent
				auto oldParent = GetParentNode(node);
				if (oldParent && oldParent != nodeParent && nodeParent != rootNode) {
					MatTransform xformToParent;
					srcNif->GetNodeTransformToParent(boneName, xformToParent);

					std::set<Ref*> childRefs;
					oldParent->GetChildRefs(childRefs);
					for (auto& ref : childRefs)
						if (ref->GetIndex() == boneID)
							ref->Clear();

					nodeParent->GetChildren().AddBlockRef(boneID);
					SetNodeTransformToParent(boneName, xformToParent);
				}
			}

			// Recurse children
			for (auto& child : srcNode->GetChildren()) {
				auto childNode = srcNif->hdr.GetBlock<NiNode>(child.GetIndex());
				if (childNode)
					cloneNodes(childNode);
			}
		};

		for (auto& child : srcRootNode->GetChildren()) {
			auto srcChildNode = srcNif->hdr.GetBlock<NiNode>(child.GetIndex());
			if (srcChildNode)
				cloneNodes(srcChildNode);
		}
	}

	// Add bones to container if used in skin
	if (destBoneCont) {
		for (auto& boneName : srcBoneList) {
			auto node = FindBlockByName<NiNode>(boneName);
			int boneID = GetBlockID(node);
			if (node)
				destBoneCont->GetBones().AddBlockRef(boneID);
		}
	}

	return destShape;
}

int NifFile::CloneNamedNode(const std::string& nodeName, NifFile* srcNif) {
	if (!srcNif)
		srcNif = this;

	auto srcNode = srcNif->FindBlockByName<NiNode>(nodeName);
	if (!srcNode)
		return NIF_NPOS;

	auto destNode = srcNode->Clone();
	destNode->SetName(nodeName);
	destNode->SetCollisionRef(NIF_NPOS);
	destNode->SetControllerRef(NIF_NPOS);
	destNode->GetChildren().Clear();
	destNode->GetEffects().Clear();

	return hdr.AddBlock(destNode);
}

int NifFile::Save(const std::filesystem::path& fileName, const NifSaveOptions& options) {
	std::fstream file(fileName, std::ios::out | std::ios::binary);
	return Save(file, options);
}

int NifFile::Save(std::iostream& file, const NifSaveOptions& options) {
	if (file) {
		if (hdr.GetVersion().IsFO76())
			return 76;

		NiStream stream(&file, &hdr.GetVersion());
		FinalizeData();

		if (options.optimize)
			Optimize();

		if (options.sortBlocks)
			PrettySortBlocks();

		hdr.Put(stream);
		stream.InitBlockSize();

		// Retrieve block sizes from NiStream while writing
		std::vector<int> blockSizes(hdr.GetNumBlocks());
		for (uint32_t i = 0; i < hdr.GetNumBlocks(); i++) {
			blocks[i]->Put(stream);
			blockSizes[i] = stream.GetBlockSize();
			stream.InitBlockSize();
		}

		uint32_t endPad = 1;
		stream << endPad;
		endPad = 0;
		stream << endPad;

		// Get previous stream pos of block size array and overwrite
		std::streampos blockSizePos = hdr.GetBlockSizeStreamPos();
		if (blockSizePos != std::streampos()) {
			file.seekg(blockSizePos);

			for (uint32_t i = 0; i < hdr.GetNumBlocks(); i++)
				stream << blockSizes[i];

			hdr.ResetBlockSizeStreamPos();
		}
	}
	else
		return 1;

	return 0;
}

void NifFile::Optimize() {
	for (auto& s : GetShapes())
		s->UpdateBounds();

	DeleteUnreferencedBlocks();
}

OptResult NifFile::OptimizeFor(OptOptions& options) {
	OptResult result;

	if (options.targetVersion.IsSSE() && hdr.GetVersion().IsSK()) {
		// SK -> SSE
		if (!isTerrain)
			result.dupesRenamed = RenameDuplicateShapes();

		hdr.SetVersion(options.targetVersion);

		for (auto& shape : GetShapes()) {
			std::string shapeName = shape->GetName();

			auto geomData = hdr.GetBlock<NiGeometryData>(shape->GetDataRef());
			if (geomData) {
				bool removeVertexColors = true;
				bool hasTangents = geomData->HasTangents();
				std::vector<Vector3>* vertices = &geomData->vertices;
				std::vector<Vector3>* normals = &geomData->normals;
				const std::vector<Color4>& colors = geomData->vertexColors;
				std::vector<Vector2>* uvs = nullptr;
				if (!geomData->uvSets.empty())
					uvs = &geomData->uvSets[0];

				std::vector<Triangle> triangles;
				geomData->GetTriangles(triangles);

				if (!options.removeParallax)
					removeVertexColors = false;

				// Only remove vertex colors if all are 0xFFFFFFFF
				if (removeVertexColors) {
					Color4 white(1.0f, 1.0f, 1.0f, 1.0f);
					for (auto& c : colors) {
						if (white != c) {
							removeVertexColors = false;
							break;
						}
					}
				}

				bool headPartEyes = false;
				NiShader* shader = GetShader(shape);
				if (shader) {
					auto bslsp = dynamic_cast<BSLightingShaderProperty*>(shader);
					if (bslsp) {
						// Remember eyes flag for later
						if ((bslsp->shaderFlags1 & (1 << 17)) != 0)
							headPartEyes = true;

						// No normals and tangents with model space maps
						if (bslsp->IsModelSpace()) {
							if (!normals->empty())
								result.shapesNormalsRemoved.push_back(shapeName);

							normals = nullptr;
						}

						// Check tree anim flag
						if ((bslsp->shaderFlags2 & (1 << 29)) != 0)
							removeVertexColors = false;

						// Disable flags if vertex colors were removed
						if (removeVertexColors) {
							bslsp->SetVertexColors(false);
							bslsp->SetVertexAlpha(false);
						}

						if (options.removeParallax) {
							if (bslsp->GetShaderType() == BSLSP_PARALLAX) {
								// Change type from parallax to default
								bslsp->SetShaderType(BSLSP_DEFAULT);

								// Remove parallax flag
								bslsp->shaderFlags1 &= ~(1 << 11);

								// Remove parallax texture from set
								auto textureSet = hdr.GetBlock<BSShaderTextureSet>(shader->GetTextureSetRef());
								if (textureSet && textureSet->numTextures >= 4)
									textureSet->textures[3].Clear();

								result.shapesParallaxRemoved.push_back(shapeName);
							}
						}
					}

					auto bsesp = dynamic_cast<BSEffectShaderProperty*>(shader);
					if (bsesp) {
						// Remember eyes flag for later
						if ((bsesp->shaderFlags1 & (1 << 17)) != 0)
							headPartEyes = true;

						// Check tree anim flag
						if ((bsesp->shaderFlags2 & (1 << 29)) != 0)
							removeVertexColors = false;

						// Disable flags if vertex colors were removed
						if (removeVertexColors) {
							bsesp->SetVertexColors(false);
							bsesp->SetVertexAlpha(false);
						}
					}
				}

				if (!colors.empty() && removeVertexColors)
					result.shapesVColorsRemoved.push_back(shapeName);

				BSTriShape* bsOptShape = nullptr;

				auto bsSegmentShape = dynamic_cast<BSSegmentedTriShape*>(shape);
				if (bsSegmentShape) {
					bsOptShape = new BSSubIndexTriShape();
				}
				else {
					if (options.headParts)
						bsOptShape = new BSDynamicTriShape();
					else
						bsOptShape = new BSTriShape();
				}

				bsOptShape->SetName(shape->GetName());
				bsOptShape->SetControllerRef(shape->GetControllerRef());
				bsOptShape->SetSkinInstanceRef(shape->GetSkinInstanceRef());
				bsOptShape->SetShaderPropertyRef(shape->GetShaderPropertyRef());
				bsOptShape->SetAlphaPropertyRef(shape->GetAlphaPropertyRef());
				bsOptShape->SetCollisionRef(shape->GetCollisionRef());
				bsOptShape->GetProperties() = shape->GetProperties();
				bsOptShape->GetExtraData() = shape->GetExtraData();

				bsOptShape->SetTransformToParent(shape->GetTransformToParent());

				bsOptShape->Create(hdr.GetVersion(), vertices, &triangles, uvs, normals);
				bsOptShape->flags = shape->flags;

				// Move segments to new shape
				if (bsSegmentShape) {
					auto bsSITS = dynamic_cast<BSSubIndexTriShape*>(bsOptShape);
					bsSITS->numSegments = bsSegmentShape->numSegments;
					bsSITS->segments = bsSegmentShape->segments;
				}

				// Restore old bounds for static meshes or when calc bounds is off
				if (!shape->IsSkinned() || !options.calcBounds)
					bsOptShape->SetBounds(geomData->GetBounds());

				// Vertex Colors
				if (bsOptShape->GetNumVertices() > 0) {
					if (!removeVertexColors && !colors.empty()) {
						bsOptShape->SetVertexColors(true);
						for (uint32_t i = 0; i < bsOptShape->GetNumVertices(); i++) {
							auto& vertex = bsOptShape->vertData[i];

							float f = std::max(0.0f, std::min(1.0f, colors[i].r));
							vertex.colorData[0] = static_cast<uint8_t>(
								std::floor(f == 1.0f ? 255 : f * 256.0));

							f = std::max(0.0f, std::min(1.0f, colors[i].g));
							vertex.colorData[1] = static_cast<uint8_t>(
								std::floor(f == 1.0f ? 255 : f * 256.0));

							f = std::max(0.0f, std::min(1.0f, colors[i].b));
							vertex.colorData[2] = static_cast<uint8_t>(
								std::floor(f == 1.0f ? 255 : f * 256.0));

							f = std::max(0.0f, std::min(1.0f, colors[i].a));
							vertex.colorData[3] = static_cast<uint8_t>(
								std::floor(f == 1.0f ? 255 : f * 256.0));
						}
					}

					// Find NiOptimizeKeep string
					for (auto& extraData : bsOptShape->GetExtraData()) {
						auto stringData = hdr.GetBlock<NiStringExtraData>(extraData.GetIndex());
						if (stringData) {
							if (stringData->GetStringData().find("NiOptimizeKeep") != std::string::npos) {
								bsOptShape->particleDataSize = bsOptShape->GetNumVertices() * 6
															   + triangles.size() * 3;
								bsOptShape->particleVerts = *vertices;

								bsOptShape->particleNorms.resize(vertices->size(), Vector3(1.0f, 0.0f, 0.0f));
								if (normals && normals->size() == vertices->size())
									bsOptShape->particleNorms = *normals;

								bsOptShape->particleTris = triangles;
							}
						}
					}

					// Skinning and partitions
					if (shape->IsSkinned()) {
						bsOptShape->SetSkinned(true);

						auto skinInst = hdr.GetBlock<NiSkinInstance>(shape->GetSkinInstanceRef());
						if (skinInst) {
							auto skinPart = hdr.GetBlock<NiSkinPartition>(skinInst->GetSkinPartitionRef());
							if (skinPart) {
								bool triangulated = skinPart->ConvertStripsToTriangles();
								if (triangulated)
									result.shapesPartTriangulated.push_back(shapeName);

								for (uint32_t partID = 0; partID < skinPart->numPartitions; partID++) {
									NiSkinPartition::PartitionBlock& part = skinPart->partitions[partID];

									for (uint32_t i = 0; i < part.numVertices; i++) {
										const uint16_t v = part.vertexMap[i];

										if (bsOptShape->vertData.size() > v) {
											auto& vertex = bsOptShape->vertData[v];

											if (part.hasVertexWeights) {
												auto& weights = part.vertexWeights[i];
												vertex.weights[0] = weights.w1;
												vertex.weights[1] = weights.w2;
												vertex.weights[2] = weights.w3;
												vertex.weights[3] = weights.w4;
											}

											if (part.hasBoneIndices) {
												auto& boneIndices = part.boneIndices[i];
												vertex.weightBones[0] = part.bones[boneIndices.i1];
												vertex.weightBones[1] = part.bones[boneIndices.i2];
												vertex.weightBones[2] = part.bones[boneIndices.i3];
												vertex.weightBones[3] = part.bones[boneIndices.i4];
											}
										}
									}

									part.GenerateTrueTrianglesFromMappedTriangles();
									part.triangles = part.trueTriangles;
								}
								skinPart->bMappedIndices = false;
							}
						}
					}
					else
						bsOptShape->SetSkinned(false);
				}
				else
					bsOptShape->SetVertices(false);

				// Check if tangents were added
				if (!hasTangents && bsOptShape->HasTangents())
					result.shapesTangentsAdded.push_back(shapeName);

				// Enable eye data flag
				if (!bsSegmentShape) {
					if (options.headParts) {
						if (headPartEyes)
							bsOptShape->SetEyeData(true);
					}
				}

				hdr.ReplaceBlock(GetBlockID(shape), bsOptShape);
				UpdateSkinPartitions(bsOptShape);
			}
		}

		DeleteUnreferencedBlocks();

		// For files without a root node, remove the leftover data blocks anyway
		hdr.DeleteBlockByType("NiTriStripsData", true);
		hdr.DeleteBlockByType("NiTriShapeData", true);
	}
	else if (options.targetVersion.IsSK() && hdr.GetVersion().IsSSE()) {
		// SSE -> SK
		if (!isTerrain)
			result.dupesRenamed = RenameDuplicateShapes();

		hdr.SetVersion(options.targetVersion);

		for (auto& shape : GetShapes()) {
			std::string shapeName = shape->GetName();

			auto bsTriShape = dynamic_cast<BSTriShape*>(shape);
			if (bsTriShape) {
				bool removeVertexColors = true;
				bool hasTangents = bsTriShape->HasTangents();
				std::vector<Vector3>* vertices = bsTriShape->GetRawVerts();
				std::vector<Vector3>* normals = bsTriShape->GetNormalData(false);
				std::vector<Color4>* colors = bsTriShape->GetColorData();
				std::vector<Vector2>* uvs = bsTriShape->GetUVData();

				std::vector<Triangle> triangles;
				bsTriShape->GetTriangles(triangles);

				if (!options.removeParallax)
					removeVertexColors = false;

				// Only remove vertex colors if all are 0xFFFFFFFF
				if (colors && removeVertexColors) {
					Color4 white(1.0f, 1.0f, 1.0f, 1.0f);
					for (auto& c : (*colors)) {
						if (white != c) {
							removeVertexColors = false;
							break;
						}
					}
				}

				NiShader* shader = GetShader(shape);
				if (shader) {
					auto bslsp = dynamic_cast<BSLightingShaderProperty*>(shader);
					if (bslsp) {
						// No normals and tangents with model space maps
						if (bslsp->IsModelSpace()) {
							if (normals && !normals->empty())
								result.shapesNormalsRemoved.push_back(shapeName);

							normals = nullptr;
						}

						// Check tree anim flag
						if ((bslsp->shaderFlags2 & (1 << 29)) != 0)
							removeVertexColors = false;

						// Disable flags if vertex colors were removed
						if (removeVertexColors) {
							bslsp->SetVertexColors(false);
							bslsp->SetVertexAlpha(false);
						}

						if (options.removeParallax) {
							if (bslsp->GetShaderType() == BSLSP_PARALLAX) {
								// Change type from parallax to default
								bslsp->SetShaderType(BSLSP_DEFAULT);

								// Remove parallax flag
								bslsp->shaderFlags1 &= ~(1 << 11);

								// Remove parallax texture from set
								auto textureSet = hdr.GetBlock<BSShaderTextureSet>(shader->GetTextureSetRef());
								if (textureSet && textureSet->numTextures >= 4)
									textureSet->textures[3].Clear();

								result.shapesParallaxRemoved.push_back(shapeName);
							}
						}
					}

					auto bsesp = dynamic_cast<BSEffectShaderProperty*>(shader);
					if (bsesp) {
						// Check tree anim flag
						if ((bsesp->shaderFlags2 & (1 << 29)) != 0)
							removeVertexColors = false;

						// Disable flags if vertex colors were removed
						if (removeVertexColors) {
							bsesp->SetVertexColors(false);
							bsesp->SetVertexAlpha(false);
						}
					}
				}

				if (colors && !colors->empty() && removeVertexColors)
					result.shapesVColorsRemoved.push_back(shapeName);

				NiTriShape* bsOptShape = nullptr;
				auto bsOptShapeData = new NiTriShapeData();
				auto bsSITS = dynamic_cast<BSSubIndexTriShape*>(shape);
				if (bsSITS)
					bsOptShape = new BSSegmentedTriShape();
				else
					bsOptShape = new NiTriShape();

				int dataId = hdr.AddBlock(bsOptShapeData);
				bsOptShape->SetDataRef(dataId);
				bsOptShape->SetGeomData(bsOptShapeData);
				bsOptShapeData->Create(hdr.GetVersion(), vertices, &triangles, uvs, normals);

				bsOptShape->SetName(shape->GetName());
				bsOptShape->SetControllerRef(shape->GetControllerRef());
				bsOptShape->SetSkinInstanceRef(shape->GetSkinInstanceRef());
				bsOptShape->SetShaderPropertyRef(shape->GetShaderPropertyRef());
				bsOptShape->SetAlphaPropertyRef(shape->GetAlphaPropertyRef());
				bsOptShape->SetCollisionRef(shape->GetCollisionRef());
				bsOptShape->GetProperties() = shape->GetProperties();
				bsOptShape->GetExtraData() = shape->GetExtraData();

				bsOptShape->SetTransformToParent(shape->GetTransformToParent());
				bsOptShape->flags = shape->flags;

				// Move segments to new shape
				if (bsSITS) {
					auto bsSegmentShape = dynamic_cast<BSSegmentedTriShape*>(bsOptShape);
					bsSegmentShape->numSegments = bsSITS->numSegments;
					bsSegmentShape->segments = bsSITS->segments;
				}

				// Restore old bounds for static meshes or when calc bounds is off
				if (!shape->IsSkinned() || !options.calcBounds)
					bsOptShape->SetBounds(bsTriShape->GetBounds());

				// Vertex Colors
				if (bsOptShape->GetNumVertices() > 0) {
					if (!removeVertexColors && colors && !colors->empty()) {
						bsOptShape->SetVertexColors(true);
						for (uint32_t i = 0; i < bsOptShape->GetNumVertices(); i++)
							bsOptShapeData->vertexColors[i] = (*colors)[i];
					}

					// Skinning and partitions
					if (shape->IsSkinned()) {
						auto skinInst = hdr.GetBlock<NiSkinInstance>(shape->GetSkinInstanceRef());
						if (skinInst) {
							auto skinPart = hdr.GetBlock<NiSkinPartition>(skinInst->GetSkinPartitionRef());
							if (skinPart) {
								bool triangulated = skinPart->ConvertStripsToTriangles();
								if (triangulated)
									result.shapesPartTriangulated.push_back(shapeName);

								for (uint32_t partID = 0; partID < skinPart->numPartitions; partID++) {
									NiSkinPartition::PartitionBlock& part = skinPart->partitions[partID];

									part.GenerateMappedTrianglesFromTrueTrianglesAndVertexMap();
								}
								skinPart->bMappedIndices = true;
							}
						}
					}
				}
				else
					bsOptShape->SetVertices(false);

				// Check if tangents were added
				if (!hasTangents && bsOptShape->HasTangents())
					result.shapesTangentsAdded.push_back(shapeName);

				hdr.ReplaceBlock(GetBlockID(shape), bsOptShape);
				UpdateSkinPartitions(bsOptShape);
			}
		}

		DeleteUnreferencedBlocks();
		PrettySortBlocks();
	}
	else {
		result.versionMismatch = true;
	}

	return result;
}

void NifFile::PrepareData() {
	hdr.FillStringRefs();
	LinkGeomData();
	TrimTexturePaths();

	for (auto& shape : GetShapes()) {
		// Move triangle and vertex data from partition to shape
		if (hdr.GetVersion().User() >= 12 && hdr.GetVersion().Stream() == 100) {
			auto* bsTriShape = dynamic_cast<BSTriShape*>(shape);
			if (!bsTriShape)
				continue;

			auto skinInst = hdr.GetBlock<NiSkinInstance>(shape->GetSkinInstanceRef());
			if (!skinInst)
				continue;

			auto skinPart = hdr.GetBlock<NiSkinPartition>(skinInst->GetSkinPartitionRef());
			if (!skinPart)
				continue;

			bsTriShape->SetVertexData(skinPart->vertData);

			std::vector<Triangle> tris;
			for (size_t pi = 0; pi < skinPart->partitions.size(); ++pi)
				for (auto& tri : skinPart->partitions[pi].trueTriangles) {
					tris.push_back(tri);
					skinPart->triParts.push_back(pi);
				}

			bsTriShape->SetTriangles(tris);

			auto dynamicShape = dynamic_cast<BSDynamicTriShape*>(bsTriShape);
			if (dynamicShape) {
				for (uint32_t i = 0; i < dynamicShape->GetNumVertices(); i++) {
					dynamicShape->vertData[i].vert.x = dynamicShape->dynamicData[i].x;
					dynamicShape->vertData[i].vert.y = dynamicShape->dynamicData[i].y;
					dynamicShape->vertData[i].vert.z = dynamicShape->dynamicData[i].z;
					dynamicShape->vertData[i].bitangentX = dynamicShape->dynamicData[i].w;
				}
			}
		}
	}

	RemoveInvalidTris();
}

void NifFile::FinalizeData() {
	for (auto& shape : GetShapes()) {
		auto bsTriShape = dynamic_cast<BSTriShape*>(shape);
		if (bsTriShape) {
			auto bsDynTriShape = dynamic_cast<BSDynamicTriShape*>(shape);
			if (bsDynTriShape)
				bsDynTriShape->CalcDynamicData();

			bsTriShape->CalcDataSizes(hdr.GetVersion());

			if (hdr.GetVersion().User() >= 12 && hdr.GetVersion().Stream() == 100) {
				// Move triangle and vertex data from shape to partition
				auto skinInst = hdr.GetBlock<NiSkinInstance>(shape->GetSkinInstanceRef());
				if (skinInst) {
					auto skinPart = hdr.GetBlock<NiSkinPartition>(skinInst->GetSkinPartitionRef());
					if (skinPart) {
						skinPart->numVertices = bsTriShape->GetNumVertices();
						skinPart->dataSize = bsTriShape->dataSize;
						skinPart->vertexSize = bsTriShape->vertexSize;
						skinPart->vertData = bsTriShape->vertData;
						skinPart->vertexDesc = bsTriShape->vertexDesc;

						for (uint32_t partInd = 0; partInd < skinPart->numPartitions; ++partInd) {
							NiSkinPartition::PartitionBlock& part = skinPart->partitions[partInd];

							// Copy relevant data from shape to each partition
							part.vertexDesc = bsTriShape->vertexDesc;
						}
					}
				}
			}
		}
	}

	hdr.UpdateHeaderStrings(hasUnknown);
}

bool NifFile::IsSSECompatible() {
	auto shapes = GetShapes();
	return std::all_of(shapes.cbegin(), shapes.cend(), [this](auto&& shape) {
		return IsSSECompatible(shape);
	});
}

bool NifFile::IsSSECompatible(NiShape* shape) {
	// Check if shape has strips in the geometry or skin partition
	if (shape->HasType<NiTriStrips>())
		return false;

	auto skinInst = hdr.GetBlock<NiSkinInstance>(shape->GetSkinInstanceRef());
	if (skinInst) {
		auto skinPart = hdr.GetBlock<NiSkinPartition>(skinInst->GetSkinPartitionRef());
		if (skinPart) {
			for (auto& partition : skinPart->partitions) {
				if (partition.numStrips > 0)
					return false;
			}
		}
	}

	return true;
}

std::vector<std::string> NifFile::GetShapeNames() {
	std::vector<std::string> outList;
	for (auto& block : blocks) {
		auto shape = dynamic_cast<NiShape*>(block.get());
		if (shape)
			outList.push_back(shape->GetName());
	}
	return outList;
}

std::vector<NiShape*> NifFile::GetShapes() {
	std::vector<NiShape*> outList;
	for (auto& block : blocks) {
		auto shape = dynamic_cast<NiShape*>(block.get());
		if (shape)
			outList.push_back(shape);
	}
	return outList;
}

bool NifFile::RenameShape(NiShape* shape, const std::string& newName) {
	if (shape) {
		shape->SetName(newName);
		return true;
	}

	return false;
}

bool NifFile::RenameDuplicateShapes() {
	auto countDupes = [this](NiNode* parent, const std::string& name) {
		if (name.empty())
			return ptrdiff_t(0);

		std::vector<std::string> names;
		std::set<int> uniqueRefs;
		for (auto& child : parent->GetChildren()) {
			int childIndex = child.GetIndex();
			auto obj = hdr.GetBlock<NiAVObject>(childIndex);
			if (obj) {
				if (uniqueRefs.find(childIndex) == uniqueRefs.end()) {
					names.push_back(obj->GetName());
					uniqueRefs.insert(childIndex);
				}
			}
		}

		return std::count(names.begin(), names.end(), name);
	};

	bool renamed = false;
	auto nodes = GetChildren<NiNode>();

	auto root = GetRootNode();
	if (root)
		nodes.push_back(root);

	for (auto& node : nodes) {
		int dupCount = 0;

		for (auto& child : node->GetChildren()) {
			auto shape = hdr.GetBlock<NiShape>(child.GetIndex());
			if (shape) {
				// Skip first child
				if (dupCount == 0) {
					dupCount++;
					continue;
				}

				std::string shapeName = shape->GetName();

				bool duped = countDupes(node, shapeName) > 1;
				if (duped) {
					std::string dup = "_" + std::to_string(dupCount);

					while (countDupes(node, shapeName + dup) > 1) {
						dupCount++;
						dup = "_" + std::to_string(dupCount);
					}

					shape->SetName(shapeName + dup);
					dupCount++;
					renamed = true;
				}
			}
		}
	}

	return renamed;
}

void NifFile::TriangulateShape(NiShape* shape) {
	if (shape->HasType<NiTriStrips>()) {
		auto stripsData = hdr.GetBlock<NiTriStripsData>(shape->GetDataRef());
		if (stripsData) {
			std::vector<Triangle> tris = stripsData->StripsToTris();

			if (!tris.empty()) {
				auto triShape = new NiTriShape();
				*static_cast<NiTriBasedGeom*>(triShape) = *static_cast<NiTriBasedGeom*>(shape);
				hdr.ReplaceBlock(GetBlockID(shape), triShape);

				auto triShapeData = new NiTriShapeData();
				*static_cast<NiTriBasedGeomData*>(triShapeData) = *static_cast<NiTriBasedGeomData*>(
					stripsData);
				triShapeData->SetTriangles(tris);
				hdr.ReplaceBlock(GetBlockID(stripsData), triShapeData);
				triShape->SetGeomData(triShapeData);
			}
		}
	}
}

NiNode* NifFile::GetRootNode() {
	// Check if block at index 0 is a node
	auto root = hdr.GetBlock<NiNode>(0);
	if (!root) {
		// Not a node, look for first node block
		for (auto& block : blocks) {
			auto node = dynamic_cast<NiNode*>(block.get());
			if (node) {
				root = node;
				break;
			}
		}
	}
	return root;
}

void NifFile::GetTree(std::vector<NiObject*>& result, NiObject* parent) {
	if (parent == nullptr) {
		parent = GetRootNode();
		if (parent == nullptr)
			return;
	}

	auto findResult = [&result = result](NiObject* obj) -> bool {
		return (std::find(result.begin(), result.end(), obj) != result.end());
	};

	std::vector<int> indices;
	parent->GetChildIndices(indices);

	auto constraint = dynamic_cast<bhkConstraint*>(parent);
	if (constraint) {
		for (auto& entityId : constraint->GetEntities()) {
			auto entity = hdr.GetBlock<NiObject>(entityId.GetIndex());
			if (entity && !findResult(entity))
				GetTree(result, entity);
		}
	}

	for (auto& id : indices) {
		auto child = hdr.GetBlock<NiObject>(id);
		if (child && !findResult(child)) {
			bool childBeforeParent = child->HasType<bhkRefObject>() && !child->HasType<bhkConstraint>();
			if (childBeforeParent)
				GetTree(result, child);
		}
	}

	result.push_back(parent);

	for (auto& id : indices) {
		auto child = hdr.GetBlock<NiObject>(id);
		if (child && !findResult(child)) {
			bool childBeforeParent = child->HasType<bhkRefObject>() && !child->HasType<bhkConstraint>();
			if (!childBeforeParent)
				GetTree(result, child);
		}
	}
}

bool NifFile::GetNodeTransformToParent(const std::string& nodeName, MatTransform& outTransform) {
	for (auto& block : blocks) {
		auto node = dynamic_cast<NiNode*>(block.get());
		if (node && node->GetName() == nodeName) {
			outTransform = node->GetTransformToParent();
			return true;
		}
	}
	return false;
}

bool NifFile::GetNodeTransformToGlobal(const std::string& nodeName, MatTransform& outTransform) {
	for (auto& block : blocks) {
		auto* node = dynamic_cast<NiNode*>(block.get());
		if (!node || node->GetName() != nodeName)
			continue;

		MatTransform xform = node->GetTransformToParent();
		NiNode* parent = GetParentNode(node);
		while (parent) {
			xform = parent->GetTransformToParent().ComposeTransforms(xform);
			parent = GetParentNode(parent);
		}
		outTransform = xform;
		return true;
	}

	return false;
}

bool NifFile::SetNodeTransformToParent(const std::string& nodeName,
									   const MatTransform& inTransform,
									   const bool rootChildrenOnly) {
	if (rootChildrenOnly) {
		auto root = GetRootNode();
		if (root) {
			for (auto& child : root->GetChildren()) {
				auto node = hdr.GetBlock<NiNode>(child.GetIndex());
				if (node) {
					if (node->GetName() == nodeName) {
						node->SetTransformToParent(inTransform);
						return true;
					}
				}
			}
		}
	}
	else {
		for (auto& block : blocks) {
			auto node = dynamic_cast<NiNode*>(block.get());
			if (node && node->GetName() == nodeName) {
				node->SetTransformToParent(inTransform);
				return true;
			}
		}
	}

	return false;
}

int NifFile::GetShapeBoneList(NiShape* shape, std::vector<std::string>& outList) {
	outList.clear();

	if (!shape)
		return 0;

	auto skinInst = hdr.GetBlock<NiBoneContainer>(shape->GetSkinInstanceRef());
	if (!skinInst)
		return 0;

	auto& bones = skinInst->GetBones();
	for (uint32_t i = 0; i < bones.GetSize(); i++) {
		auto node = hdr.GetBlock<NiNode>(bones.GetBlockRef(i));
		if (node)
			outList.push_back(node->GetName());
	}

	return outList.size();
}

int NifFile::GetShapeBoneIDList(NiShape* shape, std::vector<int>& outList) {
	outList.clear();

	if (!shape)
		return 0;

	auto skinInst = hdr.GetBlock<NiBoneContainer>(shape->GetSkinInstanceRef());
	if (!skinInst)
		return 0;

	auto& bones = skinInst->GetBones();
	for (uint32_t i = 0; i < bones.GetSize(); i++)
		outList.push_back(bones.GetBlockRef(i));

	return outList.size();
}

void NifFile::SetShapeBoneIDList(NiShape* shape, std::vector<int>& inList) {
	if (!shape)
		return;

	if (shape->GetSkinInstanceRef() == NIF_NPOS)
		return;

	BSSkinBoneData* boneData = nullptr;
	if (shape->HasType<BSTriShape>()) {
		auto skinForBoneRef = hdr.GetBlock<BSSkinInstance>(shape->GetSkinInstanceRef());
		if (skinForBoneRef)
			boneData = hdr.GetBlock<BSSkinBoneData>(skinForBoneRef->GetDataRef());
	}

	auto boneCont = hdr.GetBlock<NiBoneContainer>(shape->GetSkinInstanceRef());
	if (!boneCont)
		return;

	boneCont->GetBones().Clear();

	bool feedBoneData = false;
	if (boneData && boneData->nBones != inList.size()) {
		// Clear if size doesn't match
		boneData->nBones = 0;
		boneData->boneXforms.clear();
		feedBoneData = true;
	}

	for (auto& i : inList) {
		boneCont->GetBones().AddBlockRef(i);
		if (boneData && feedBoneData) {
			boneData->boneXforms.emplace_back();
			boneData->nBones++;
		}
	}

	auto skinInst = dynamic_cast<NiSkinInstance*>(boneCont);
	if (skinInst) {
		auto skinData = hdr.GetBlock<NiSkinData>(skinInst->GetDataRef());
		if (skinData) {
			feedBoneData = false;

			if (skinData->numBones != inList.size()) {
				// Clear if size doesn't match
				skinData->numBones = 0;
				skinData->bones.clear();
				feedBoneData = true;
			}

			if (feedBoneData) {
				skinData->bones.resize(inList.size());
				skinData->numBones = skinData->bones.size();
			}
		}
	}
}

int NifFile::GetShapeBoneWeights(NiShape* shape,
								 const int boneIndex,
								 std::unordered_map<uint16_t, float>& outWeights) {
	outWeights.clear();

	if (!shape)
		return 0;

	auto bsTriShape = dynamic_cast<BSTriShape*>(shape);
	if (bsTriShape) {
		outWeights.reserve(bsTriShape->GetNumVertices());
		for (int vid = 0; vid < bsTriShape->GetNumVertices(); vid++) {
			auto& vertex = bsTriShape->vertData[vid];
			for (uint32_t i = 0; i < 4; i++) {
				if (vertex.weightBones[i] == boneIndex && vertex.weights[i] != 0.0f)
					outWeights.emplace(vid, vertex.weights[i]);
			}
		}

		return outWeights.size();
	}

	auto skinInst = hdr.GetBlock<NiSkinInstance>(shape->GetSkinInstanceRef());
	if (!skinInst)
		return 0;

	auto skinData = hdr.GetBlock<NiSkinData>(skinInst->GetDataRef());
	if (!skinData || boneIndex >= skinData->numBones)
		return 0;

	NiSkinData::BoneData* bone = &skinData->bones[boneIndex];
	for (auto& sw : bone->vertexWeights)
		if (sw.weight >= EPSILON)
			outWeights.emplace(sw.index, sw.weight);

	return outWeights.size();
}

bool NifFile::CalcShapeTransformGlobalToSkin(NiShape* shape, MatTransform& outTransform) {
	if (!shape)
		return false;
	if (GetShapeTransformGlobalToSkin(shape, outTransform))
		return true;

	// Now the nif doesn't have this transform, probably because it's
	// a FO4 nif, so we will try to calculate it, since FO4 shapes almost
	// always have a non-identity global-to-skin transform.
	// Ideally, we'd use bone transforms from the skeleton file, but we
	// don't have access to that here.
	std::vector<std::string> bones;
	GetShapeBoneList(shape, bones);
	for (const std::string& bone : bones) {
		MatTransform xformBoneToGlobal;
		if (!GetNodeTransformToGlobal(bone, xformBoneToGlobal))
			continue;
		MatTransform xformSkinToBone;
		if (!GetShapeTransformSkinToBone(shape, bone, xformSkinToBone))
			continue;
		// compose: skin -> bone -> global and invert
		outTransform = xformBoneToGlobal.ComposeTransforms(xformSkinToBone).InverseTransform();
		return true;
	}
	return false;
}

bool NifFile::GetShapeTransformGlobalToSkin(NiShape* shape, MatTransform& outTransform) {
	if (!shape)
		return false;

	// For FO4 meshes, the skin instance is a BSSkinInstance instead of
	// an NiSkinInstance, so skinInst will be nullptr.  FO4 meshes do not
	// have this transform.
	auto skinInst = hdr.GetBlock<NiSkinInstance>(shape->GetSkinInstanceRef());
	if (!skinInst)
		return false;

	auto skinData = hdr.GetBlock<NiSkinData>(skinInst->GetDataRef());
	if (!skinData)
		return false;

	outTransform = skinData->skinTransform;
	return true;
}

void NifFile::SetShapeTransformGlobalToSkin(NiShape* shape, const MatTransform& inTransform) {
	if (!shape)
		return;

	// For FO4 meshes, the skin instance is a BSSkinInstance instead of
	// an NiSkinInstance, so skinInst will be nullptr.  FO4 meshes do not
	// have this transform.
	auto skinInst = hdr.GetBlock<NiSkinInstance>(shape->GetSkinInstanceRef());
	if (!skinInst)
		return;

	auto skinData = hdr.GetBlock<NiSkinData>(skinInst->GetDataRef());
	if (!skinData)
		return;

	// Set the overall skin transform
	skinData->skinTransform = inTransform;
}

bool NifFile::GetShapeTransformSkinToBone(NiShape* shape,
										  const std::string& boneName,
										  MatTransform& outTransform) {
	if (!shape)
		return false;
	return GetShapeTransformSkinToBone(shape, shape->GetBoneID(hdr, boneName), outTransform);
}

bool NifFile::GetShapeTransformSkinToBone(NiShape* shape, const int boneIndex, MatTransform& outTransform) {
	if (!shape)
		return false;

	auto skinForBoneRef = hdr.GetBlock<BSSkinInstance>(shape->GetSkinInstanceRef());
	if (skinForBoneRef) {
		auto boneData = hdr.GetBlock<BSSkinBoneData>(skinForBoneRef->GetDataRef());
		if (boneData) {
			if (boneIndex >= boneData->nBones)
				return false;

			outTransform = boneData->boneXforms[boneIndex].boneTransform;
			return true;
		}
	}

	auto skinInst = hdr.GetBlock<NiSkinInstance>(shape->GetSkinInstanceRef());
	if (!skinInst)
		return false;

	auto skinData = hdr.GetBlock<NiSkinData>(skinInst->GetDataRef());
	if (!skinData)
		return false;

	if (boneIndex >= skinData->numBones)
		return false;

	NiSkinData::BoneData* bone = &skinData->bones[boneIndex];
	outTransform = bone->boneTransform;
	return true;
}

void NifFile::SetShapeTransformSkinToBone(NiShape* shape,
										  const int boneIndex,
										  const MatTransform& inTransform) {
	if (!shape)
		return;

	auto skinForBoneRef = hdr.GetBlock<BSSkinInstance>(shape->GetSkinInstanceRef());
	if (skinForBoneRef) {
		auto bsSkin = hdr.GetBlock<BSSkinBoneData>(skinForBoneRef->GetDataRef());
		if (!bsSkin)
			return;

		if (boneIndex >= bsSkin->nBones)
			return;
		bsSkin->boneXforms[boneIndex].boneTransform = inTransform;
		return;
	}

	auto skinInst = hdr.GetBlock<NiSkinInstance>(shape->GetSkinInstanceRef());
	if (!skinInst)
		return;

	auto skinData = hdr.GetBlock<NiSkinData>(skinInst->GetDataRef());
	if (!skinData)
		return;

	if (boneIndex >= skinData->numBones)
		return;

	NiSkinData::BoneData* bone = &skinData->bones[boneIndex];
	bone->boneTransform = inTransform;
}

bool NifFile::GetShapeBoneTransform(NiShape* shape, const std::string& boneName, MatTransform& outTransform) {
	if (boneName.empty())
		return GetShapeTransformGlobalToSkin(shape, outTransform);
	return GetShapeTransformSkinToBone(shape, boneName, outTransform);
}

bool NifFile::GetShapeBoneTransform(NiShape* shape, const int boneIndex, MatTransform& outTransform) {
	if (boneIndex == 0xFFFFFFFF)
		return GetShapeTransformGlobalToSkin(shape, outTransform);

	return GetShapeTransformSkinToBone(shape, boneIndex, outTransform);
}

bool NifFile::SetShapeBoneTransform(NiShape* shape, const int boneIndex, MatTransform& inTransform) {
	if (boneIndex == 0xFFFFFFFF)
		SetShapeTransformGlobalToSkin(shape, inTransform);
	else
		SetShapeTransformSkinToBone(shape, boneIndex, inTransform);
	return true;
}

bool NifFile::SetShapeBoneBounds(const std::string& shapeName, const int boneIndex, BoundingSphere& inBounds) {
	auto shape = FindBlockByName<NiShape>(shapeName);
	if (!shape)
		return false;

	auto skinForBoneRef = hdr.GetBlock<BSSkinInstance>(shape->GetSkinInstanceRef());
	if (skinForBoneRef && boneIndex != 0xFFFFFFFF) {
		auto bsSkin = hdr.GetBlock<BSSkinBoneData>(skinForBoneRef->GetDataRef());
		if (!bsSkin)
			return false;

		bsSkin->boneXforms[boneIndex].bounds = inBounds;
		return true;
	}

	auto skinInst = hdr.GetBlock<NiSkinInstance>(shape->GetSkinInstanceRef());
	if (!skinInst)
		return false;

	auto skinData = hdr.GetBlock<NiSkinData>(skinInst->GetDataRef());
	if (!skinData)
		return false;

	if (boneIndex >= skinData->numBones)
		return false;

	NiSkinData::BoneData* bone = &skinData->bones[boneIndex];
	bone->bounds = inBounds;
	return true;
}

bool NifFile::GetShapeBoneBounds(NiShape* shape, const int boneIndex, BoundingSphere& outBounds) {
	if (!shape)
		return false;

	auto skinForBoneRef = hdr.GetBlock<BSSkinInstance>(shape->GetSkinInstanceRef());
	if (skinForBoneRef) {
		auto boneData = hdr.GetBlock<BSSkinBoneData>(skinForBoneRef->GetDataRef());
		if (boneData) {
			outBounds = boneData->boneXforms[boneIndex].bounds;
			return true;
		}
	}

	auto skinInst = hdr.GetBlock<NiSkinInstance>(shape->GetSkinInstanceRef());
	if (!skinInst)
		return false;

	auto skinData = hdr.GetBlock<NiSkinData>(skinInst->GetDataRef());
	if (!skinData)
		return false;

	if (boneIndex >= skinData->numBones)
		return false;

	NiSkinData::BoneData* bone = &skinData->bones[boneIndex];
	outBounds = bone->bounds;
	return true;
}

void NifFile::UpdateShapeBoneID(const std::string& shapeName, const int oldID, const int newID) {
	auto shape = FindBlockByName<NiShape>(shapeName);
	if (!shape)
		return;

	auto boneCont = hdr.GetBlock<NiBoneContainer>(shape->GetSkinInstanceRef());
	if (!boneCont)
		return;

	for (auto& bp : boneCont->GetBones()) {
		if (bp.GetIndex() == oldID) {
			bp.SetIndex(newID);
			return;
		}
	}
}

// Not implemented for BSTriShape, use SetShapeVertWeights instead
void NifFile::SetShapeBoneWeights(const std::string& shapeName,
								  const int boneIndex,
								  std::unordered_map<uint16_t, float>& inWeights) {
	auto shape = FindBlockByName<NiShape>(shapeName);
	if (!shape)
		return;

	auto skinInst = hdr.GetBlock<NiSkinInstance>(shape->GetSkinInstanceRef());
	if (!skinInst)
		return;

	auto skinData = hdr.GetBlock<NiSkinData>(skinInst->GetDataRef());
	if (!skinData)
		return;

	if (boneIndex >= skinData->numBones)
		return;

	NiSkinData::BoneData* bone = &skinData->bones[boneIndex];
	bone->vertexWeights.clear();
	for (auto& sw : inWeights)
		if (sw.second >= 0.0001f)
			bone->vertexWeights.emplace_back(SkinWeight(sw.first, sw.second));

	bone->numVertices = static_cast<uint16_t>(bone->vertexWeights.size());
}

void NifFile::SetShapeVertWeights(const std::string& shapeName,
								  const int vertIndex,
								  std::vector<uint8_t>& boneids,
								  std::vector<float>& weights) {
	auto shape = FindBlockByName<NiShape>(shapeName);
	if (!shape)
		return;

	auto bsTriShape = dynamic_cast<BSTriShape*>(shape);
	if (!bsTriShape)
		return;

	if (vertIndex < 0 || vertIndex >= bsTriShape->vertData.size())
		return;

	auto& vertex = bsTriShape->vertData[vertIndex];
	std::memset(vertex.weights, 0, sizeof(float) * 4);
	std::memset(vertex.weightBones, 0, sizeof(uint8_t) * 4);

	// Sum weights to normalize values
	float sum = 0.0f;
	for (auto weight : weights)
		sum += weight;

	int num = (weights.size() < 4 ? weights.size() : 4);

	for (int i = 0; i < num; i++) {
		vertex.weightBones[i] = boneids[i];
		vertex.weights[i] = weights[i] / sum;
	}
}

void NifFile::ClearShapeVertWeights(const std::string& shapeName) {
	auto shape = FindBlockByName<NiShape>(shapeName);
	if (!shape)
		return;

	auto bsTriShape = dynamic_cast<BSTriShape*>(shape);
	if (!bsTriShape)
		return;

	for (auto& vertex : bsTriShape->vertData) {
		std::memset(vertex.weights, 0, sizeof(float) * 4);
		std::memset(vertex.weightBones, 0, sizeof(uint8_t) * 4);
	}
}

bool NifFile::GetShapeSegments(NiShape* shape, NifSegmentationInfo& inf, std::vector<int>& triParts) {
	auto bssits = dynamic_cast<BSSubIndexTriShape*>(shape);
	if (!bssits)
		return false;

	bssits->GetSegmentation(inf, triParts);
	return true;
}

void NifFile::SetShapeSegments(NiShape* shape,
							   const NifSegmentationInfo& inf,
							   const std::vector<int>& triParts) {
	auto bssits = dynamic_cast<BSSubIndexTriShape*>(shape);
	if (!bssits)
		return;

	bssits->SetSegmentation(inf, triParts);
}

bool NifFile::GetShapePartitions(NiShape* shape,
								 std::vector<BSDismemberSkinInstance::PartitionInfo>& partitionInfo,
								 std::vector<int>& triParts) {
	if (!shape)
		return false;

	auto bsdSkinInst = hdr.GetBlock<BSDismemberSkinInstance>(shape->GetSkinInstanceRef());
	if (bsdSkinInst)
		partitionInfo = bsdSkinInst->GetPartitions();
	else
		partitionInfo.clear();

	auto skinInst = hdr.GetBlock<NiSkinInstance>(shape->GetSkinInstanceRef());
	if (!skinInst)
		return false;

	auto skinPart = hdr.GetBlock<NiSkinPartition>(skinInst->GetSkinPartitionRef());
	if (!skinPart)
		return false;

	// Generate triParts
	std::vector<Triangle> shapeTris;
	shape->GetTriangles(shapeTris);
	skinPart->PrepareTriParts(shapeTris);
	triParts = skinPart->triParts;

	// Make sure every partition has a PartitionInfo
	while (partitionInfo.size() < skinPart->partitions.size()) {
		BSDismemberSkinInstance::PartitionInfo pi;
		pi.flags = PF_EDITOR_VISIBLE;
		pi.partID = hdr.GetVersion().User() >= 12 ? 32 : 0;
		partitionInfo.push_back(pi);
	}

	return true;
}

void NifFile::SetShapePartitions(NiShape* shape,
								 const std::vector<BSDismemberSkinInstance::PartitionInfo>& partitionInfo,
								 const std::vector<int>& triParts,
								 const bool convertSkinInstance) {
	if (!shape)
		return;

	auto skinInst = hdr.GetBlock<NiSkinInstance>(shape->GetSkinInstanceRef());
	if (!skinInst)
		return;

	auto skinPart = hdr.GetBlock<NiSkinPartition>(skinInst->GetSkinPartitionRef());
	if (!skinPart)
		return;

	// Calculate new number of partitions.  This code assumes we might have
	// misassigned or unassigned triangles, though it's unclear whether
	// it's even possible to have misassigned or unassigned triangles.
	int numParts = partitionInfo.size();
	bool hasUnassignedTris = false;
	for (int pi : triParts) {
		if (pi >= numParts)
			numParts = pi + 1;
		if (pi < 0)
			hasUnassignedTris = true;
	}
	if (hasUnassignedTris)
		++numParts;

	// Copy triParts and assign unassigned triangles to a partition
	skinPart->triParts = triParts;
	if (hasUnassignedTris) {
		for (int& pi : skinPart->triParts) {
			if (pi < 0)
				pi = numParts - 1;
		}
	}

	// Resize NiSkinPartition partition list
	skinPart->numPartitions = numParts;
	skinPart->partitions.resize(numParts);
	for (int i = 0; i < numParts; i++)
		skinPart->partitions[i].hasVertexMap = true;

	// Regenerate trueTriangles
	std::vector<Triangle> shapeTris;
	shape->GetTriangles(shapeTris);
	skinPart->GenerateTrueTrianglesFromTriParts(shapeTris);

	// Set BSDismemberSkinInstance partition list
	auto bsdSkinInst = hdr.GetBlock<BSDismemberSkinInstance>(shape->GetSkinInstanceRef());
	if (!bsdSkinInst && convertSkinInstance) {
		bsdSkinInst = new BSDismemberSkinInstance();
		*static_cast<NiSkinInstance*>(bsdSkinInst) = *static_cast<NiSkinInstance*>(skinInst);
		hdr.ReplaceBlock(GetBlockID(skinInst), bsdSkinInst);
	}

	if (bsdSkinInst) {
		bsdSkinInst->SetPartitions(partitionInfo);
		while (bsdSkinInst->GetNumPartitions() < numParts) {
			BSDismemberSkinInstance::PartitionInfo pi;
			pi.flags = PF_EDITOR_VISIBLE;
			pi.partID = hdr.GetVersion().User() >= 12 ? 32 : 0;
			bsdSkinInst->AddPartition(pi);
		}
	}
}

void NifFile::SetDefaultPartition(NiShape* shape) {
	std::vector<Triangle> tris;
	shape->GetTriangles(tris);

	std::vector<Vector3> verts;
	bool bMappedIndices = true;
	if (shape->HasType<NiTriShape>()) {
		auto shapeData = hdr.GetBlock<NiTriShapeData>(shape->GetDataRef());
		if (!shapeData)
			return;

		verts = shapeData->vertices;
	}
	else if (shape->HasType<NiTriStrips>()) {
		auto stripsData = hdr.GetBlock<NiTriStripsData>(shape->GetDataRef());
		if (!stripsData)
			return;

		verts = stripsData->vertices;
	}
	else if (shape->HasType<BSTriShape>()) {
		auto bsTriShape = dynamic_cast<BSTriShape*>(shape);
		if (!bsTriShape)
			return;

		auto rawVerts = bsTriShape->GetRawVerts();
		if (rawVerts)
			verts = (*rawVerts);

		bMappedIndices = false;
	}

	auto bsdSkinInst = hdr.GetBlock<BSDismemberSkinInstance>(shape->GetSkinInstanceRef());
	if (bsdSkinInst) {
		BSDismemberSkinInstance::PartitionInfo partInfo;
		partInfo.flags = PF_EDITOR_VISIBLE;
		partInfo.partID = hdr.GetVersion().User() >= 12 ? 32 : 0;

		bsdSkinInst->ClearPartitions();
		bsdSkinInst->AddPartition(partInfo);
	}

	auto skinInst = hdr.GetBlock<NiSkinInstance>(shape->GetSkinInstanceRef());
	if (!skinInst)
		return;

	auto skinPart = hdr.GetBlock<NiSkinPartition>(skinInst->GetSkinPartitionRef());
	if (skinPart) {
		NiSkinPartition::PartitionBlock part;
		if (!verts.empty()) {
			part.hasVertexMap = true;
			part.numVertices = verts.size();

			std::vector<uint16_t> vertIndices(part.numVertices);
			for (size_t i = 0; i < vertIndices.size(); i++)
				vertIndices[i] = i;

			part.vertexMap = vertIndices;
		}

		if (!tris.empty()) {
			part.numTriangles = tris.size();
			part.trueTriangles = tris;
			if (!bMappedIndices)
				part.triangles = part.trueTriangles;
		}

		skinPart->bMappedIndices = bMappedIndices;
		skinPart->partitions.clear();
		skinPart->partitions.push_back(part);
		skinPart->numPartitions = 1;
		skinPart->triParts.clear();
	}
}

void NifFile::DeletePartitions(NiShape* shape, std::vector<int>& partInds) {
	if (!shape)
		return;

	auto skinInst = hdr.GetBlock<NiSkinInstance>(shape->GetSkinInstanceRef());
	if (!skinInst)
		return;

	auto skinPart = hdr.GetBlock<NiSkinPartition>(skinInst->GetSkinPartitionRef());
	if (!skinPart)
		return;

	skinPart->DeletePartitions(partInds);

	auto bsdSkinInst = dynamic_cast<BSDismemberSkinInstance*>(skinInst);
	if (bsdSkinInst) {
		bsdSkinInst->DeletePartitions(partInds);
		UpdatePartitionFlags(shape);
	}
}

const std::vector<Vector3>* NifFile::GetRawVertsForShape(NiShape* shape) {
	if (!shape)
		return nullptr;

	if (shape->HasType<NiTriBasedGeom>()) {
		auto geomData = hdr.GetBlock<NiGeometryData>(shape->GetDataRef());
		if (geomData)
			return &geomData->vertices;
	}
	else if (shape->HasType<BSTriShape>()) {
		auto bsTriShape = dynamic_cast<BSTriShape*>(shape);
		if (bsTriShape)
			return bsTriShape->GetRawVerts();
	}

	return nullptr;
}

bool NifFile::ReorderTriangles(NiShape* shape, const std::vector<uint32_t>& triangleIndices) {
	if (!shape)
		return false;

	if (shape->HasType<NiTriStrips>())
		return false;

	return shape->ReorderTriangles(triangleIndices);
}

const std::vector<Vector3>* NifFile::GetNormalsForShape(NiShape* shape, bool transform) {
	if (!shape || !shape->HasNormals())
		return nullptr;

	if (shape->HasType<NiTriBasedGeom>()) {
		auto geomData = hdr.GetBlock<NiGeometryData>(shape->GetDataRef());
		if (geomData)
			return &geomData->normals;
	}
	else if (shape->HasType<BSTriShape>()) {
		auto bsTriShape = dynamic_cast<BSTriShape*>(shape);
		if (bsTriShape)
			return bsTriShape->GetNormalData(transform);
	}

	return nullptr;
}

const std::vector<Vector2>* NifFile::GetUvsForShape(NiShape* shape) {
	if (!shape)
		return nullptr;

	if (shape->HasType<NiTriBasedGeom>()) {
		auto geomData = hdr.GetBlock<NiGeometryData>(shape->GetDataRef());
		if (geomData && !geomData->uvSets.empty())
			return &geomData->uvSets[0];
	}
	else if (shape->HasType<BSTriShape>()) {
		auto bsTriShape = dynamic_cast<BSTriShape*>(shape);
		if (bsTriShape)
			return bsTriShape->GetUVData();
	}

	return nullptr;
}

const std::vector<Color4>* NifFile::GetColorsForShape(const std::string& shapeName) {
	auto shape = FindBlockByName<NiShape>(shapeName);
	if (!shape)
		return nullptr;

	if (shape->HasType<NiTriBasedGeom>()) {
		auto geomData = hdr.GetBlock<NiGeometryData>(shape->GetDataRef());
		if (geomData)
			return &geomData->vertexColors;
	}
	else if (shape->HasType<BSTriShape>()) {
		auto bsTriShape = dynamic_cast<BSTriShape*>(shape);
		if (bsTriShape)
			return bsTriShape->GetColorData();
	}

	return nullptr;
}

const std::vector<Vector3>* NifFile::GetTangentsForShape(NiShape* shape, bool transform) {
	if (!shape || !shape->HasTangents())
		return nullptr;

	if (shape->HasType<NiTriBasedGeom>()) {
		auto geomData = hdr.GetBlock<NiGeometryData>(shape->GetDataRef());
		if (geomData)
			return &geomData->tangents;
	}
	else if (shape->HasType<BSTriShape>()) {
		auto bsTriShape = dynamic_cast<BSTriShape*>(shape);
		if (bsTriShape)
			return bsTriShape->GetTangentData(transform);
	}

	return nullptr;
}

const std::vector<Vector3>* NifFile::GetBitangentsForShape(NiShape* shape, bool transform) {
	if (!shape || !shape->HasTangents())
		return nullptr;

	if (shape->HasType<NiTriBasedGeom>()) {
		auto geomData = hdr.GetBlock<NiGeometryData>(shape->GetDataRef());
		if (geomData)
			return &geomData->bitangents;
	}
	else if (shape->HasType<BSTriShape>()) {
		auto bsTriShape = dynamic_cast<BSTriShape*>(shape);
		if (bsTriShape)
			return bsTriShape->GetBitangentData(transform);
	}

	return nullptr;
}

std::vector<float>* NifFile::GetEyeDataForShape(NiShape* shape) {
	if (!shape)
		return nullptr;

	auto bsTriShape = dynamic_cast<BSTriShape*>(shape);
	if (bsTriShape)
		return bsTriShape->GetEyeData();

	return nullptr;
}

bool NifFile::GetUvsForShape(NiShape* shape, std::vector<Vector2>& outUvs) {
	const std::vector<Vector2>* uvData = GetUvsForShape(shape);
	if (uvData) {
		outUvs.assign(uvData->begin(), uvData->end());
		return true;
	}

	return false;
}

bool NifFile::GetVertsForShape(NiShape* shape, std::vector<Vector3>& outVerts) {
	if (!shape) {
		outVerts.clear();
		return false;
	}

	if (shape->HasType<NiTriBasedGeom>()) {
		auto geomData = hdr.GetBlock<NiGeometryData>(shape->GetDataRef());
		if (geomData) {
			outVerts = geomData->vertices;
			return true;
		}
	}
	else if (shape->HasType<BSTriShape>()) {
		auto bsTriShape = dynamic_cast<BSTriShape*>(shape);
		if (bsTriShape) {
			outVerts.resize(bsTriShape->GetNumVertices());

			for (uint32_t i = 0; i < bsTriShape->GetNumVertices(); i++)
				outVerts[i] = bsTriShape->vertData[i].vert;

			return true;
		}
	}

	outVerts.clear();
	return false;
}

void NifFile::SetVertsForShape(NiShape* shape, const std::vector<Vector3>& verts) {
	if (!shape)
		return;

	if (shape->HasType<NiTriBasedGeom>()) {
		auto geomData = hdr.GetBlock<NiGeometryData>(shape->GetDataRef());
		if (geomData) {
			if (verts.size() != geomData->vertices.size())
				geomData->Create(hdr.GetVersion(), &verts, nullptr, nullptr, nullptr);
			else
				geomData->vertices = verts;
		}
	}
	else if (shape->HasType<BSTriShape>()) {
		auto bsTriShape = dynamic_cast<BSTriShape*>(shape);
		if (bsTriShape) {
			if (verts.size() != bsTriShape->GetNumVertices()) {
				bsTriShape->Create(hdr.GetVersion(), &verts, nullptr, nullptr, nullptr);
			}
			else {
				for (uint32_t i = 0; i < bsTriShape->GetNumVertices(); i++)
					bsTriShape->vertData[i].vert = verts[i];
			}
		}
	}
}

void NifFile::SetUvsForShape(NiShape* shape, const std::vector<Vector2>& uvs) {
	if (!shape)
		return;

	if (shape->HasType<NiTriBasedGeom>()) {
		auto geomData = hdr.GetBlock<NiGeometryData>(shape->GetDataRef());
		if (geomData) {
			if (uvs.size() != geomData->vertices.size())
				return;

			geomData->SetUVs(true);
			geomData->uvSets[0] = uvs;
		}
	}
	else if (shape->HasType<BSTriShape>()) {
		auto bsTriShape = dynamic_cast<BSTriShape*>(shape);
		if (bsTriShape) {
			if (uvs.size() != bsTriShape->vertData.size())
				return;

			bsTriShape->SetUVs(true);

			for (uint32_t i = 0; i < bsTriShape->GetNumVertices(); i++)
				bsTriShape->vertData[i].uv = uvs[i];
		}
	}
}

void NifFile::SetColorsForShape(const std::string& shapeName, const std::vector<Color4>& colors) {
	auto shape = FindBlockByName<NiShape>(shapeName);
	if (!shape)
		return;

	if (shape->HasType<NiTriBasedGeom>()) {
		auto geomData = hdr.GetBlock<NiGeometryData>(shape->GetDataRef());
		if (geomData) {
			if (colors.size() != geomData->vertices.size())
				return;

			geomData->SetVertexColors(true);
			geomData->vertexColors = colors;
		}
	}
	else if (shape->HasType<BSTriShape>()) {
		auto bsTriShape = dynamic_cast<BSTriShape*>(shape);
		if (bsTriShape) {
			if (colors.size() != bsTriShape->vertData.size())
				return;

			bsTriShape->SetVertexColors(true);

			for (uint32_t i = 0; i < bsTriShape->GetNumVertices(); i++) {
				auto& vertex = bsTriShape->vertData[i];

				float f = std::max(0.0f, std::min(1.0f, colors[i].r));
				vertex.colorData[0] = static_cast<uint8_t>(std::floor(f == 1.0f ? 255 : f * 256.0));

				f = std::max(0.0f, std::min(1.0f, colors[i].g));
				vertex.colorData[1] = static_cast<uint8_t>(std::floor(f == 1.0f ? 255 : f * 256.0));

				f = std::max(0.0f, std::min(1.0f, colors[i].b));
				vertex.colorData[2] = static_cast<uint8_t>(std::floor(f == 1.0f ? 255 : f * 256.0));

				f = std::max(0.0f, std::min(1.0f, colors[i].a));
				vertex.colorData[3] = static_cast<uint8_t>(std::floor(f == 1.0f ? 255 : f * 256.0));
			}
		}
	}
}

void NifFile::SetTangentsForShape(NiShape* shape, const std::vector<Vector3>& in) {
	if (!shape)
		return;

	if (shape->HasType<NiTriBasedGeom>()) {
		auto geomData = hdr.GetBlock<NiGeometryData>(shape->GetDataRef());
		if (geomData) {
			geomData->SetTangents(true);
			geomData->tangents = in;
		}
	}
	else if (shape->HasType<BSTriShape>()) {
		auto bsTriShape = dynamic_cast<BSTriShape*>(shape);
		if (bsTriShape)
			bsTriShape->SetTangentData(in);
	}
}

void NifFile::SetBitangentsForShape(NiShape* shape, const std::vector<Vector3>& in) {
	if (!shape)
		return;

	if (shape->HasType<NiTriBasedGeom>()) {
		auto geomData = hdr.GetBlock<NiGeometryData>(shape->GetDataRef());
		if (geomData) {
			geomData->SetTangents(true);
			geomData->bitangents = in;
		}
	}
	else if (shape->HasType<BSTriShape>()) {
		auto bsTriShape = dynamic_cast<BSTriShape*>(shape);
		if (bsTriShape)
			bsTriShape->SetBitangentData(in);
	}
}

void NifFile::SetEyeDataForShape(NiShape* shape, const std::vector<float>& in) {
	if (!shape)
		return;

	auto bsTriShape = dynamic_cast<BSTriShape*>(shape);
	if (bsTriShape)
		bsTriShape->SetEyeData(in);
}

void NifFile::InvertUVsForShape(NiShape* shape, bool invertX, bool invertY) {
	if (!shape)
		return;

	if (shape->HasType<NiTriBasedGeom>()) {
		auto geomData = hdr.GetBlock<NiGeometryData>(shape->GetDataRef());
		if (geomData && !geomData->uvSets.empty()) {
			if (invertX)
				for (auto& i : geomData->uvSets[0])
					i.u = 1.0f - i.u;

			if (invertY)
				for (auto& i : geomData->uvSets[0])
					i.v = 1.0f - i.v;
		}
	}
	else if (shape->HasType<BSTriShape>()) {
		auto bsTriShape = dynamic_cast<BSTriShape*>(shape);
		if (bsTriShape) {
			if (invertX)
				for (auto& i : bsTriShape->vertData)
					i.uv.u = 1.0f - i.uv.u;

			if (invertY)
				for (auto& i : bsTriShape->vertData)
					i.uv.v = 1.0f - i.uv.v;
		}
	}
}

void NifFile::MirrorShape(NiShape* shape, bool mirrorX, bool mirrorY, bool mirrorZ) {
	if (!shape)
		return;

	bool flipTris = false;
	Matrix4 mirrorMat;

	if (mirrorX) {
		mirrorMat.Scale(-1.0f, 1.0f, 1.0f);
		flipTris = !flipTris;
	}

	if (mirrorY) {
		mirrorMat.Scale(1.0f, -1.0f, 1.0f);
		flipTris = !flipTris;
	}

	if (mirrorZ) {
		mirrorMat.Scale(1.0f, 1.0f, -1.0f);
		flipTris = !flipTris;
	}

	if (shape->HasType<NiTriBasedGeom>()) {
		auto geomData = hdr.GetBlock<NiGeometryData>(shape->GetDataRef());
		if (geomData && !geomData->vertices.empty()) {
			for (auto& vertice : geomData->vertices)
				vertice = mirrorMat * vertice;

			for (auto& normal : geomData->normals)
				normal = mirrorMat * normal;

			for (auto& tangent : geomData->tangents)
				tangent = mirrorMat * tangent;

			for (auto& bitangent : geomData->bitangents)
				bitangent = mirrorMat * bitangent;
		}
	}
	else if (shape->HasType<BSTriShape>()) {
		auto bsTriShape = dynamic_cast<BSTriShape*>(shape);
		if (bsTriShape) {
			for (auto& i : bsTriShape->vertData)
				i.vert = mirrorMat * i.vert;

			auto normals = bsTriShape->GetNormalData(false);
			if (normals) {
				for (auto& normal : *normals)
					normal = mirrorMat * normal;

				bsTriShape->SetNormals((*normals));

				if (bsTriShape->HasTangents())
					bsTriShape->CalcTangentSpace();
			}
		}
	}

	if (flipTris) {
		std::vector<Triangle> tris;
		shape->GetTriangles(tris);

		for (auto& tri : tris)
			std::swap(tri.p1, tri.p3);

		shape->SetTriangles(tris);
	}
}

void NifFile::SetNormalsForShape(NiShape* shape, const std::vector<Vector3>& norms) {
	if (!shape)
		return;

	if (shape->HasType<NiTriBasedGeom>()) {
		auto geomData = hdr.GetBlock<NiGeometryData>(shape->GetDataRef());
		if (geomData) {
			geomData->SetNormals(true);
			geomData->normals = norms;
		}
	}
	else if (shape->HasType<BSTriShape>()) {
		auto bsTriShape = dynamic_cast<BSTriShape*>(shape);
		if (bsTriShape)
			bsTriShape->SetNormals(norms);
	}
}

void NifFile::CalcNormalsForShape(NiShape* shape,
								  const bool force,
								  const bool smooth,
								  const float smoothThresh) {
	if (!shape)
		return;

	if (hdr.GetVersion().IsSK() || hdr.GetVersion().IsSSE()) {
		NiShader* shader = GetShader(shape);
		if (shader && shader->IsModelSpace() && !force)
			return;
	}

	std::unordered_set<uint32_t> lockedIndices;

	for (auto& extraDataRef : shape->GetExtraData()) {
		auto integersExtraData = hdr.GetBlock<NiIntegersExtraData>(extraDataRef.GetIndex());
		if (integersExtraData && integersExtraData->GetName() == "LOCKEDNORM")
			for (auto& i : integersExtraData->GetIntegersData())
				lockedIndices.insert(i);
	}

	if (shape->HasType<NiTriBasedGeom>()) {
		auto geomData = hdr.GetBlock<NiGeometryData>(shape->GetDataRef());
		if (geomData)
			geomData->RecalcNormals(smooth, smoothThresh);
	}
	else if (shape->HasType<BSTriShape>()) {
		auto bsTriShape = dynamic_cast<BSTriShape*>(shape);
		if (bsTriShape)
			bsTriShape->RecalcNormals(smooth, smoothThresh, &lockedIndices);
	}
}

void NifFile::CalcTangentsForShape(NiShape* shape) {
	if (!shape)
		return;

	if (shape->HasType<NiTriBasedGeom>()) {
		auto geomData = hdr.GetBlock<NiGeometryData>(shape->GetDataRef());
		if (geomData)
			geomData->CalcTangentSpace();
	}
	else if (shape->HasType<BSTriShape>()) {
		auto bsTriShape = dynamic_cast<BSTriShape*>(shape);
		if (bsTriShape)
			bsTriShape->CalcTangentSpace();
	}
}

int NifFile::ApplyNormalsFromFile(NifFile& srcNif, const std::string& shapeName) {
	auto shape = FindBlockByName<NiShape>(shapeName);
	if (!shape)
		return -1;

	auto srcShape = srcNif.FindBlockByName<NiShape>(shapeName);
	if (!srcShape)
		return -2;

	std::unordered_set<uint32_t> lockedNormalIndices;

	// Get LOCKEDNORM from source
	NiIntegersExtraData* integersExtraData = nullptr;

	for (auto& extraDataRef : srcShape->GetExtraData()) {
		integersExtraData = srcNif.GetHeader().GetBlock<NiIntegersExtraData>(extraDataRef.GetIndex());
		if (integersExtraData && integersExtraData->GetName() == "LOCKEDNORM")
			for (auto& i : integersExtraData->GetIntegersData())
				lockedNormalIndices.insert(i);
	}

	if (lockedNormalIndices.empty())
		return -3;

	// Get normals of target
	auto norms = GetNormalsForShape(shape, false);
	if (!norms)
		return -4;

	// Get normals of source
	auto srcNorms = srcNif.GetNormalsForShape(srcShape, false);
	if (!srcNorms)
		return -5;

	// Vertex count needs to match up
	if (norms->size() != srcNorms->size())
		return -6;

	auto workNorms = (*norms);

	// Copy locked normals of the source into the target
	for (auto& i : lockedNormalIndices) {
		auto& sn = srcNorms->at(i);
		workNorms[i] = sn;
	}

	SetNormalsForShape(shape, workNorms);

	for (auto& extraDataRef : shape->GetExtraData()) {
		auto oldIntegersExtraData = hdr.GetBlock<NiIntegersExtraData>(extraDataRef.GetIndex());
		if (oldIntegersExtraData && oldIntegersExtraData->GetName() == "LOCKEDNORM")
			hdr.DeleteBlock(extraDataRef.GetIndex());
	}

	auto cloneIntegersExtraData = integersExtraData->Clone();
	AssignExtraData(shape, cloneIntegersExtraData);
	return 0;
}

void NifFile::GetRootTranslation(Vector3& outVec) {
	auto root = GetRootNode();
	if (root)
		outVec = root->GetTransformToParent().translation;
	else
		outVec.Zero();
}

void NifFile::MoveVertex(NiShape* shape, const Vector3& pos, const int id) {
	if (!shape)
		return;

	if (shape->HasType<NiTriBasedGeom>()) {
		auto geomData = hdr.GetBlock<NiGeometryData>(shape->GetDataRef());
		if (geomData && geomData->GetNumVertices() > id)
			geomData->vertices[id] = pos;
	}
	else if (shape->HasType<BSTriShape>()) {
		auto bsTriShape = dynamic_cast<BSTriShape*>(shape);
		if (bsTriShape && bsTriShape->GetNumVertices() > id)
			bsTriShape->vertData[id].vert = pos;
	}
}

void NifFile::OffsetShape(NiShape* shape, const Vector3& offset, std::unordered_map<uint16_t, float>* mask) {
	if (!shape)
		return;

	if (shape->HasType<NiTriBasedGeom>()) {
		auto geomData = hdr.GetBlock<NiGeometryData>(shape->GetDataRef());
		if (geomData) {
			for (size_t i = 0; i < geomData->vertices.size(); i++) {
				if (mask) {
					float maskFactor = 1.0f;
					Vector3 diff = offset;
					if (mask->find(i) != mask->end()) {
						maskFactor = 1.0f - (*mask)[i];
						diff *= maskFactor;
					}
					geomData->vertices[i] += diff;
				}
				else
					geomData->vertices[i] += offset;
			}
		}
	}
	else if (shape->HasType<BSTriShape>()) {
		auto bsTriShape = dynamic_cast<BSTriShape*>(shape);
		if (bsTriShape) {
			for (uint32_t i = 0; i < bsTriShape->GetNumVertices(); i++) {
				if (mask) {
					float maskFactor = 1.0f;
					Vector3 diff = offset;
					if (mask->find(i) != mask->end()) {
						maskFactor = 1.0f - (*mask)[i];
						diff *= maskFactor;
					}
					bsTriShape->vertData[i].vert += diff;
				}
				else
					bsTriShape->vertData[i].vert += offset;
			}
		}
	}
}

void NifFile::ScaleShape(NiShape* shape, const Vector3& scale, std::unordered_map<uint16_t, float>* mask) {
	if (!shape)
		return;

	Vector3 root;
	GetRootTranslation(root);

	if (shape->HasType<NiTriBasedGeom>()) {
		auto geomData = hdr.GetBlock<NiGeometryData>(shape->GetDataRef());
		if (!geomData)
			return;

		std::unordered_map<uint16_t, Vector3> diff;
		for (size_t i = 0; i < geomData->vertices.size(); i++) {
			Vector3 target = geomData->vertices[i] - root;
			target.x *= scale.x;
			target.y *= scale.y;
			target.z *= scale.z;
			diff[i] = geomData->vertices[i] - target;

			if (mask) {
				float maskFactor = 1.0f;
				if (mask->find(i) != mask->end()) {
					maskFactor = 1.0f - (*mask)[i];
					diff[i] *= maskFactor;
					target = geomData->vertices[i] - root + diff[i];
				}
			}
			geomData->vertices[i] = target;
		}
	}
	else if (shape->HasType<BSTriShape>()) {
		auto bsTriShape = dynamic_cast<BSTriShape*>(shape);
		if (!bsTriShape)
			return;

		std::unordered_map<uint16_t, Vector3> diff;
		for (uint32_t i = 0; i < bsTriShape->GetNumVertices(); i++) {
			Vector3 target = bsTriShape->vertData[i].vert - root;
			target.x *= scale.x;
			target.y *= scale.y;
			target.z *= scale.z;
			diff[i] = bsTriShape->vertData[i].vert - target;

			if (mask) {
				float maskFactor = 1.0f;
				if (mask->find(i) != mask->end()) {
					maskFactor = 1.0f - (*mask)[i];
					diff[i] *= maskFactor;
					target = bsTriShape->vertData[i].vert - root + diff[i];
				}
			}
			bsTriShape->vertData[i].vert = target;
		}
	}
}

void NifFile::RotateShape(NiShape* shape, const Vector3& angle, std::unordered_map<uint16_t, float>* mask) {
	if (!shape)
		return;

	Vector3 root;
	GetRootTranslation(root);

	if (shape->HasType<NiTriBasedGeom>()) {
		auto geomData = hdr.GetBlock<NiGeometryData>(shape->GetDataRef());
		if (!geomData)
			return;

		std::unordered_map<uint16_t, Vector3> diff;
		for (size_t i = 0; i < geomData->vertices.size(); i++) {
			Vector3 target = geomData->vertices[i] - root;
			Matrix4 mat;
			mat.Rotate(angle.x * DEG2RAD, Vector3(1.0f, 0.0f, 0.0f));
			mat.Rotate(angle.y * DEG2RAD, Vector3(0.0f, 1.0f, 0.0f));
			mat.Rotate(angle.z * DEG2RAD, Vector3(0.0f, 0.0f, 1.0f));
			target = mat * target;
			diff[i] = geomData->vertices[i] - target;

			if (mask) {
				float maskFactor = 1.0f;
				if (mask->find(i) != mask->end()) {
					maskFactor = 1.0f - (*mask)[i];
					diff[i] *= maskFactor;
					target = geomData->vertices[i] - root + diff[i];
				}
			}
			geomData->vertices[i] = target;
		}
	}
	else if (shape->HasType<BSTriShape>()) {
		auto bsTriShape = dynamic_cast<BSTriShape*>(shape);
		if (!bsTriShape)
			return;

		std::unordered_map<uint16_t, Vector3> diff;
		for (uint32_t i = 0; i < bsTriShape->GetNumVertices(); i++) {
			Vector3 target = bsTriShape->vertData[i].vert - root;
			Matrix4 mat;
			mat.Rotate(angle.x * DEG2RAD, Vector3(1.0f, 0.0f, 0.0f));
			mat.Rotate(angle.y * DEG2RAD, Vector3(0.0f, 1.0f, 0.0f));
			mat.Rotate(angle.z * DEG2RAD, Vector3(0.0f, 0.0f, 1.0f));
			target = mat * target;
			diff[i] = bsTriShape->vertData[i].vert - target;

			if (mask) {
				float maskFactor = 1.0f;
				if (mask->find(i) != mask->end()) {
					maskFactor = 1.0f - (*mask)[i];
					diff[i] *= maskFactor;
					target = bsTriShape->vertData[i].vert - root + diff[i];
				}
			}
			bsTriShape->vertData[i].vert = target;
		}
	}
}

NiAlphaProperty* NifFile::GetAlphaProperty(NiShape* shape) {
	int alphaRef = shape->GetAlphaPropertyRef();
	if (alphaRef == NIF_NPOS) {
		for (auto& prop : shape->GetProperties()) {
			auto alphaProp = hdr.GetBlock<NiAlphaProperty>(prop.GetIndex());
			if (alphaProp) {
				alphaRef = prop.GetIndex();
				break;
			}
		}
	}

	return hdr.GetBlock<NiAlphaProperty>(alphaRef);
}

int NifFile::AssignAlphaProperty(NiShape* shape, NiAlphaProperty* alphaProp) {
	RemoveAlphaProperty(shape);

	NiShader* shader = GetShader(shape);
	if (shader) {
		int alphaRef = hdr.AddBlock(alphaProp);
		if (shader->HasType<BSShaderPPLightingProperty>())
			shape->GetProperties().AddBlockRef(alphaRef);
		else
			shape->SetAlphaPropertyRef(alphaRef);

		return alphaRef;
	}

	return NIF_NPOS;
}

void NifFile::RemoveAlphaProperty(NiShape* shape) {
	auto alpha = hdr.GetBlock<NiAlphaProperty>(shape->GetAlphaPropertyRef());
	if (alpha) {
		hdr.DeleteBlock(shape->GetAlphaPropertyRef());
		shape->SetAlphaPropertyRef(NIF_NPOS);
	}

	for (int i = 0; i < shape->GetProperties().GetSize(); i++) {
		alpha = hdr.GetBlock<NiAlphaProperty>(shape->GetProperties().GetBlockRef(i));
		if (alpha) {
			hdr.DeleteBlock(shape->GetProperties().GetBlockRef(i));
			i--;
			continue;
		}
	}
}

void NifFile::DeleteShape(NiShape* shape) {
	if (!shape)
		return;

	hdr.DeleteBlock(shape->GetDataRef());
	DeleteShader(shape);
	DeleteSkinning(shape);

	for (int i = shape->GetProperties().GetSize() - 1; i >= 0; --i)
		hdr.DeleteBlock(shape->GetProperties().GetBlockRef(i));

	for (int i = shape->GetExtraData().GetSize() - 1; i >= 0; --i)
		hdr.DeleteBlock(shape->GetExtraData().GetBlockRef(i));

	int shapeID = GetBlockID(shape);
	hdr.DeleteBlock(shapeID);
}

void NifFile::DeleteShader(NiShape* shape) {
	if (shape->GetShaderPropertyRef() != NIF_NPOS) {
		auto shader = hdr.GetBlock<NiShader>(shape->GetShaderPropertyRef());
		if (shader) {
			hdr.DeleteBlock(shader->GetTextureSetRef());
			hdr.DeleteBlock(shader->GetControllerRef());
			hdr.DeleteBlock(shape->GetShaderPropertyRef());
			shape->SetShaderPropertyRef(NIF_NPOS);
		}
	}

	RemoveAlphaProperty(shape);

	for (int i = 0; i < shape->GetProperties().GetSize(); i++) {
		auto shader = hdr.GetBlock<NiShader>(shape->GetProperties().GetBlockRef(i));
		if (shader) {
			if (shader->HasType<BSShaderPPLightingProperty>() || shader->HasType<NiMaterialProperty>()) {
				hdr.DeleteBlock(shader->GetTextureSetRef());
				hdr.DeleteBlock(shader->GetControllerRef());
				hdr.DeleteBlock(shape->GetProperties().GetBlockRef(i));
				i--;
				continue;
			}
		}
	}
}

void NifFile::DeleteSkinning(NiShape* shape) {
	auto skinInst = hdr.GetBlock<NiSkinInstance>(shape->GetSkinInstanceRef());
	if (skinInst) {
		hdr.DeleteBlock(skinInst->GetDataRef());
		hdr.DeleteBlock(skinInst->GetSkinPartitionRef());
		hdr.DeleteBlock(shape->GetSkinInstanceRef());
		shape->SetSkinInstanceRef(NIF_NPOS);
	}

	auto bsSkinInst = hdr.GetBlock<BSSkinInstance>(shape->GetSkinInstanceRef());
	if (bsSkinInst) {
		hdr.DeleteBlock(bsSkinInst->GetDataRef());
		hdr.DeleteBlock(shape->GetSkinInstanceRef());
		shape->SetSkinInstanceRef(NIF_NPOS);
	}

	shape->SetSkinned(false);

	NiShader* shader = GetShader(shape);
	if (shader)
		shader->SetSkinned(false);
}

void NifFile::RemoveEmptyPartitions(NiShape* shape) {
	if (!shape)
		return;

	int skinRef = shape->GetSkinInstanceRef();

	auto skinInst = hdr.GetBlock<NiSkinInstance>(skinRef);
	if (skinInst) {
		auto skinPartition = hdr.GetBlock<NiSkinPartition>(skinInst->GetSkinPartitionRef());
		if (skinPartition) {
			std::vector<int> emptyIndices;
			if (skinPartition->RemoveEmptyPartitions(emptyIndices)) {
				auto bsdSkinInst = dynamic_cast<BSDismemberSkinInstance*>(skinInst);
				if (bsdSkinInst) {
					bsdSkinInst->DeletePartitions(emptyIndices);
					UpdatePartitionFlags(shape);
				}
			}
		}
	}
}

bool NifFile::DeleteVertsForShape(NiShape* shape, const std::vector<uint16_t>& indices) {
	if (indices.empty())
		return false;

	if (!shape)
		return false;

	int skinRef = shape->GetSkinInstanceRef();

	auto geomData = hdr.GetBlock<NiTriBasedGeomData>(shape->GetDataRef());
	if (geomData) {
		geomData->notifyVerticesDelete(indices);
		if (geomData->GetNumVertices() == 0 || geomData->GetNumTriangles() == 0) {
			// Deleted all verts or tris
			return true;
		}
	}

	auto bsTriShape = dynamic_cast<BSTriShape*>(shape);
	if (bsTriShape) {
		bsTriShape->notifyVerticesDelete(indices);
		if (bsTriShape->GetNumVertices() == 0 || bsTriShape->GetNumTriangles() == 0) {
			// Deleted all verts or tris
			return true;
		}
	}

	auto skinInst = hdr.GetBlock<NiSkinInstance>(skinRef);
	if (skinInst) {
		auto skinData = hdr.GetBlock<NiSkinData>(skinInst->GetDataRef());
		if (skinData)
			skinData->notifyVerticesDelete(indices);

		auto skinPartition = hdr.GetBlock<NiSkinPartition>(skinInst->GetSkinPartitionRef());
		if (skinPartition) {
			skinPartition->notifyVerticesDelete(indices);

			std::vector<int> emptyIndices;
			if (skinPartition->RemoveEmptyPartitions(emptyIndices)) {
				auto bsdSkinInst = dynamic_cast<BSDismemberSkinInstance*>(skinInst);
				if (bsdSkinInst) {
					bsdSkinInst->DeletePartitions(emptyIndices);
					UpdatePartitionFlags(shape);
				}
			}
		}
	}

	for (auto& extraDataRef : shape->GetExtraData()) {
		auto integersExtraData = hdr.GetBlock<NiIntegersExtraData>(extraDataRef.GetIndex());
		if (integersExtraData && integersExtraData->GetName() == "LOCKEDNORM") {
			auto integersData = integersExtraData->GetIntegersData();
			std::sort(integersData.begin(), integersData.end());

			int highestRemoved = indices.back();
			std::vector<int> indexCollapse = GenerateIndexCollapseMap(indices, highestRemoved + 1);

			for (int i = integersData.size() - 1; i >= 0; i--) {
				auto& val = integersData[i];
				if (val > highestRemoved) {
					val -= indices.size();
				}
				else if (indexCollapse[val] == -1) {
					integersData.erase(integersData.begin() + i);
				}
				else
					val = indexCollapse[val];
			}

			integersExtraData->SetIntegersData(integersData);
		}
	}

	return false;
}

int NifFile::CalcShapeDiff(NiShape* shape,
						   const std::vector<Vector3>* targetData,
						   std::unordered_map<uint16_t, Vector3>& outDiffData,
						   float scale) {
	outDiffData.clear();

	const std::vector<Vector3>* myData = GetRawVertsForShape(shape);
	if (!myData)
		return 1;

	if (!targetData)
		return 2;

	if (myData->size() != targetData->size())
		return 3;

	for (uint32_t i = 0; i < myData->size(); i++) {
		auto& target = targetData->at(i);
		auto& src = myData->at(i);

		Vector3 v;
		v.x = (target.x * scale) - src.x;
		v.y = (target.y * scale) - src.y;
		v.z = (target.z * scale) - src.z;

		if (v.IsZero(true))
			continue;

		outDiffData[i] = v;
	}

	return 0;
}

int NifFile::CalcUVDiff(NiShape* shape,
						const std::vector<Vector2>* targetData,
						std::unordered_map<uint16_t, Vector3>& outDiffData,
						float scale) {
	outDiffData.clear();

	const std::vector<Vector2>* myData = GetUvsForShape(shape);
	if (!myData)
		return 1;

	if (!targetData)
		return 2;

	if (myData->size() != targetData->size())
		return 3;

	for (uint32_t i = 0; i < myData->size(); i++) {
		Vector3 v;
		v.x = (targetData->at(i).u - myData->at(i).u) * scale;
		v.y = (targetData->at(i).v - myData->at(i).v) * scale;

		if (v.IsZero(true))
			continue;

		outDiffData[i] = v;
	}

	return 0;
}

void NifFile::UpdateSkinPartitions(NiShape* shape) {
	NiSkinData* skinData = nullptr;
	NiSkinPartition* skinPart = nullptr;
	auto skinInst = hdr.GetBlock<NiSkinInstance>(shape->GetSkinInstanceRef());
	if (skinInst) {
		skinData = hdr.GetBlock<NiSkinData>(skinInst->GetDataRef());
		skinPart = hdr.GetBlock<NiSkinPartition>(skinInst->GetSkinPartitionRef());

		if (!skinData || !skinPart)
			return;
	}
	else
		return;

	std::vector<Triangle> tris;
	if (!shape->GetTriangles(tris))
		return;

	auto bsdSkinInst = dynamic_cast<BSDismemberSkinInstance*>(skinInst);
	auto bsTriShape = dynamic_cast<BSTriShape*>(shape);
	if (bsTriShape)
		bsTriShape->CalcDataSizes(hdr.GetVersion());

	// Align triangles for comparisons
	for (auto& t : tris)
		t.rot();

	// Make maps of vertices to bones and weights
	std::unordered_map<uint16_t, std::vector<SkinWeight>> vertBoneWeights;
	int boneIndex = 0;
	for (auto& bone : skinData->bones) {
		for (auto& bw : bone.vertexWeights)
			vertBoneWeights[bw.index].push_back(SkinWeight(boneIndex, bw.weight));

		boneIndex++;
	}

	// Sort weights and corresponding bones
	for (auto& bw : vertBoneWeights)
		sort(bw.second.begin(), bw.second.end(), BoneWeightsSort());

	// Enforce maximum vertex bone weight count
	int maxBonesPerVertex = 4;

	for (auto& bw : vertBoneWeights)
		if (bw.second.size() > maxBonesPerVertex)
			bw.second.resize(maxBonesPerVertex);

	skinPart->PrepareTriParts(tris);
	std::vector<int>& triParts = skinPart->triParts;

	int maxBonesPerPartition = std::numeric_limits<int>::max();
	if (hdr.GetVersion().IsFO3())
		maxBonesPerPartition = 18;
	else if (hdr.GetVersion().IsSSE())
		maxBonesPerPartition = 80;

	// Make a list of the bones used by each partition.  If any partition
	// has too many bones, split it.
	std::vector<std::set<int>> partBones(skinPart->partitions.size());
	for (size_t triIndex = 0; triIndex < tris.size(); ++triIndex) {
		int partInd = triParts[triIndex];
		if (partInd < 0)
			continue;

		Triangle tri = tris[triIndex];

		// Get associated bones for the current tri
		std::set<int> triBones;
		for (uint32_t i = 0; i < 3; i++)
			for (auto& tb : vertBoneWeights[tri[i]])
				triBones.insert(tb.index);

		// How many new bones are in the tri's bone list?
		int newBoneCount = 0;
		for (auto& tb : triBones)
			if (partBones[partInd].find(tb) == partBones[partInd].end())
				newBoneCount++;

		if (partBones[partInd].size() + newBoneCount > maxBonesPerPartition) {
			// Too many bones for this partition, make a new partition starting with this triangle
			for (size_t j = 0; j < tris.size(); ++j)
				if (triParts[j] > partInd || (j >= triIndex && triParts[j] >= partInd))
					++triParts[j];

			partBones.insert(partBones.begin() + partInd + 1, std::set<int>());

			if (bsdSkinInst) {
				auto partInfo = bsdSkinInst->GetPartitions();

				BSDismemberSkinInstance::PartitionInfo info;
				info.flags = PF_EDITOR_VISIBLE;
				info.partID = partInfo[partInd].partID;
				partInfo.insert(partInfo.begin() + partInd + 1, info);

				bsdSkinInst->SetPartitions(partInfo);
			}

			++partInd;
		}

		partBones[partInd].insert(triBones.begin(), triBones.end());
	}

	// Re-create partitions
	std::vector<NiSkinPartition::PartitionBlock> partitions(partBones.size());
	for (size_t partInd = 0; partInd < partBones.size(); partInd++) {
		NiSkinPartition::PartitionBlock& part = partitions[partInd];
		part.hasBoneIndices = true;
		part.hasFaces = true;
		part.hasVertexMap = true;
		part.hasVertexWeights = true;
		part.numWeightsPerVertex = maxBonesPerVertex;
	}
	skinPart->numPartitions = partitions.size();
	skinPart->partitions = std::move(partitions);

	// Re-create trueTriangles, vertexMap, and triangles for each partition
	skinPart->GenerateTrueTrianglesFromTriParts(tris);
	skinPart->PrepareVertexMapsAndTriangles();

	for (uint32_t partInd = 0; partInd < skinPart->numPartitions; ++partInd) {
		NiSkinPartition::PartitionBlock& part = skinPart->partitions[partInd];

		// Copy relevant data from shape to partition
		if (bsTriShape)
			part.vertexDesc = bsTriShape->vertexDesc;

		std::unordered_map<int, int> boneLookup;
		boneLookup.reserve(partBones[partInd].size());
		part.numBones = partBones[partInd].size();
		part.bones.reserve(part.numBones);

		for (auto& b : partBones[partInd]) {
			part.bones.push_back(b);
			boneLookup[b] = part.bones.size() - 1;
		}

		for (auto& v : part.vertexMap) {
			BoneIndices b;
			VertexWeight vw;

			uint8_t* pb = &b.i1;
			float* pw = &vw.w1;

			float tot = 0.0f;
			for (size_t bi = 0; bi < vertBoneWeights[v].size(); bi++) {
				if (bi == 4)
					break;

				pb[bi] = boneLookup[vertBoneWeights[v][bi].index];
				pw[bi] = vertBoneWeights[v][bi].weight;
				tot += pw[bi];
			}

			if (tot != 0.0f)
				for (int bi = 0; bi < 4; bi++)
					pw[bi] /= tot;

			part.boneIndices.push_back(b);
			part.vertexWeights.push_back(vw);
		}
	}

	if (bsTriShape) {
		skinPart->numVertices = bsTriShape->GetNumVertices();
		skinPart->dataSize = bsTriShape->dataSize;
		skinPart->vertexSize = bsTriShape->vertexSize;
		skinPart->vertData = bsTriShape->vertData;
		skinPart->vertexDesc = bsTriShape->vertexDesc;
	}

	UpdatePartitionFlags(shape);
}

void NifFile::UpdatePartitionFlags(NiShape* shape) {
	auto bsdSkinInst = hdr.GetBlock<BSDismemberSkinInstance>(shape->GetSkinInstanceRef());
	if (!bsdSkinInst)
		return;

	auto skinPart = hdr.GetBlock<NiSkinPartition>(bsdSkinInst->GetSkinPartitionRef());
	if (!skinPart)
		return;

	auto partInfo = bsdSkinInst->GetPartitions();
	for (size_t i = 0; i < partInfo.size(); i++) {
		PartitionFlags flags = PF_NONE;

		if (hdr.GetVersion().IsFO3()) {
			// Don't make FO3/NV meat caps visible
			if (partInfo[i].partID < 100 || partInfo[i].partID >= 1000)
				flags = PartitionFlags(flags | PF_EDITOR_VISIBLE);
		}
		else
			flags = PartitionFlags(flags | PF_EDITOR_VISIBLE);

		if (i != 0) {
			// Start a new set if the previous bones are different
			if (skinPart->partitions[i].bones != skinPart->partitions[i - 1].bones)
				flags = PartitionFlags(flags | PF_START_NET_BONESET);
		}
		else
			flags = PartitionFlags(flags | PF_START_NET_BONESET);

		partInfo[i].flags = flags;
	}

	bsdSkinInst->SetPartitions(partInfo);
}

void NifFile::CreateSkinning(NiShape* shape) {
	if (shape->HasType<NiTriShape>()) {
		if (shape->GetSkinInstanceRef() == NIF_NPOS) {
			auto nifSkinData = new NiSkinData();
			int skinDataID = hdr.AddBlock(nifSkinData);

			auto nifSkinPartition = new NiSkinPartition();
			int partID = hdr.AddBlock(nifSkinPartition);

			auto nifDismemberInst = new BSDismemberSkinInstance();
			int dismemberID = hdr.AddBlock(nifDismemberInst);

			nifDismemberInst->SetDataRef(skinDataID);
			nifDismemberInst->SetSkinPartitionRef(partID);
			nifDismemberInst->SetSkeletonRootRef(GetBlockID(GetRootNode()));
			shape->SetSkinInstanceRef(dismemberID);
			shape->SetSkinned(true);

			SetDefaultPartition(shape);
		}
	}
	else if (shape->HasType<NiTriStrips>()) {
		if (shape->GetSkinInstanceRef() == NIF_NPOS) {
			auto nifSkinData = new NiSkinData();
			int skinDataID = hdr.AddBlock(nifSkinData);

			auto nifSkinPartition = new NiSkinPartition();
			int partID = hdr.AddBlock(nifSkinPartition);

			auto nifDismemberInst = new BSDismemberSkinInstance();
			int skinID = hdr.AddBlock(nifDismemberInst);

			nifDismemberInst->SetDataRef(skinDataID);
			nifDismemberInst->SetSkinPartitionRef(partID);
			nifDismemberInst->SetSkeletonRootRef(GetBlockID(GetRootNode()));
			shape->SetSkinInstanceRef(skinID);
			shape->SetSkinned(true);

			SetDefaultPartition(shape);
		}
	}
	else if (shape->HasType<BSTriShape>()) {
		if (shape->GetSkinInstanceRef() == NIF_NPOS) {
			int skinInstID = 0;
			if (hdr.GetVersion().Stream() == 100) {
				auto nifSkinData = new NiSkinData();
				int skinDataID = hdr.AddBlock(nifSkinData);

				auto nifSkinPartition = new NiSkinPartition();
				nifSkinPartition->bMappedIndices = false;
				int partID = hdr.AddBlock(nifSkinPartition);

				auto nifDismemberInst = new BSDismemberSkinInstance();
				skinInstID = hdr.AddBlock(nifDismemberInst);

				nifDismemberInst->SetDataRef(skinDataID);
				nifDismemberInst->SetSkinPartitionRef(partID);
				nifDismemberInst->SetSkeletonRootRef(GetBlockID(GetRootNode()));

				shape->SetSkinInstanceRef(skinInstID);
				shape->SetSkinned(true);

				SetDefaultPartition(shape);
				UpdateSkinPartitions(shape);
			}
			else {
				auto newSkinInst = new BSSkinInstance();
				skinInstID = hdr.AddBlock(newSkinInst);

				auto newBoneData = new BSSkinBoneData();
				int boneDataRef = hdr.AddBlock(newBoneData);

				newSkinInst->SetTargetRef(GetBlockID(GetRootNode()));
				newSkinInst->SetDataRef(boneDataRef);

				shape->SetSkinInstanceRef(skinInstID);
				shape->SetSkinned(true);
			}
		}
	}

	NiShader* shader = GetShader(shape);
	if (shader)
		shader->SetSkinned(true);
}

void NifFile::SetShapeDynamic(const std::string& shapeName) {
	auto shape = FindBlockByName<NiShape>(shapeName);
	if (!shape)
		return;

	// Set consistency flag to mutable
	auto geomData = hdr.GetBlock<NiGeometryData>(shape->GetDataRef());
	if (geomData)
		geomData->consistencyFlags = 0;
}
