#include "internal/netimmerse.h"
#include "internal/jip_core.h"
#include "p_plus/FormModelHandler.hpp"

TempObject<NiFixedString> s_LIGH_EDID;
FormRuntimeModelManager FormRuntimeModelManager::s_nodeManager;

NiRuntimeNodeVector* TESForm::getRuntimeNodes()
{
	return FormRuntimeModelManager::getSingleton().getNodesList(this);
}

NiRuntimeNodeVector* TESForm::getFirstPersonRuntimeNodes()
{
	return &FormRuntimeModelManager::getSingleton().getFirstPersonModelList();
}

void NiNode::DownwardsInitPointLights(NiNode* root)
{
	int end = this->m_children.firstFreeEntry;
	if (end <= 0) return;

	for (auto child : this->m_children) {

		if (!child) {
			continue;
		}

		if (child->IsType(kVtbl_NiPointLight)) {
			NiPointLight* light = static_cast<NiPointLight*>(child);
			light->initLightBlock(root);
		}
		else if (child->isNiNode()) {
			((NiNode*)child)->DownwardsInitPointLights(root);
		}

	}

}

void NiNode::removeAllRuntimeNodes(NiRuntimeNodeVector* runtimeNodesVector) {
	if (runtimeNodesVector) {
		runtimeNodesVector->removeAllRuntimeNodes(this);
	}
}

void NiNode::attachAllRuntimeNodes(NiRuntimeNodeVector* runtimeNodesVector) {
	if (runtimeNodesVector && runtimeNodesVector->attachAllRuntimeNodes(this) == false) {
		//runtimeNodesVector->purgeUncachedNodes();
	}
}

void NiNode::removeRuntimeNode(const NiRuntimeNode& toRemove) {

	if (toRemove.value.isLink()) { //Is a ^parent inserted node
		removeRuntimeParent(toRemove);
	}
	else {
		removeRuntimeChild(toRemove);
	}

}

void NiNode::removeRuntimeChild(const NiRuntimeNode& toRemove) {

	NiAVObject* removeChild = nullptr;
	if (toRemove.isValid() == false) { //No cached path
		if (toRemove.node.isValid()) {
			removeChild = DeepSearchBySparsePath(NiBlockPathBase(toRemove.sparsePath, toRemove.node));
		}
	}
	else {

		uint16_t resultDepth;
		removeChild = DeepSearchByPath(NiBlockPathBase(toRemove.cachedPath, toRemove.node), resultDepth);

		if (!removeChild || toRemove.node != removeChild->m_blockName) { //Didin't find full path
			removeChild = DeepSearchBySparsePath(NiBlockPathBase(toRemove.sparsePath, toRemove.node));
		}
	}

	if (!removeChild) {
		Console_Print("JIP::removeRuntimeChild error, couldn't find the runtime child for runtime node: %s", toRemove.originToDebugString().c_str());
		return;
	}

	removeChild->m_parent->RemoveObject(removeChild);

}

void NiNode::removeRuntimeParent(const NiRuntimeNode& toRemove) {

	NiNode* removeParent = nullptr;
	if (toRemove.isValid() == false) { //No cached path

		if (toRemove.node.isValid()) {
			removeParent = (NiNode*)DeepSearchBySparsePath(toRemove.sparsePath);
		}

	}
	else {

		uint16_t resultDepth;
		NiAVObject* grandparent = DeepSearchByPath(NiBlockPathBase(toRemove.cachedPath, toRemove.node), resultDepth);

		if (!grandparent || grandparent->m_blockName != toRemove.node) { //Didin't find full path

			NiNode* child = (NiNode*)DeepSearchBySparsePath(toRemove.sparsePath);
			if (!child || child == this) {
				Console_Print("JIP::removeRuntimeParent error, couldn't find a valid child: %s", toRemove.originToDebugString().c_str());
				return;
			}

			removeParent = (NiNode*)child->findParentNode(toRemove.node, this);

		}

	}

	if (!removeParent) {
		Console_Print("JIP::removeRuntimeParent JIP Error, couldn't find the runtime parent to remove: %s", toRemove.originToDebugString().c_str());
		return;
	}

	removeParent->collapseRuntimeNode();

}

inline void NiNode::detachRuntimeNode() {
	if (!this) return;

	if (!(this->m_flags & NiAVObject::NiFlags::kNiFlag_IsInserted)) {
		Console_Print("detachRuntimeNode error: node is not a runtime node");
		return;
	}

	this->m_parent->RemoveObject(this);
}

inline void NiNode::collapseRuntimeNode() {

	if (!this) return;

	if (!(this->m_flags & NiAVObject::NiFlags::kNiFlag_IsInserted)) {
		Console_Print("collapseRuntimeNode error: node is not a runtime node");
		return;
	}

	NiNode* parent = this->m_parent;
	for (auto child : this->m_children) {

		if (!child) continue;
		parent->AddObject(child, true);

	}

	this->RemoveObject(this);

}

void NiPointLight::SetLightProperties(TESObjectLIGH* lightForm)
{
	if (!lightForm) {
		return;
	}
	// Fade distance comes straight from the form’s fadeValue (at 0xB4)
	fadeValue = lightForm->fadeValue;

	// Maximum range (radius) and falloff exponent
	radius = static_cast<float>(lightForm->radius);
	radius0E4 = lightForm->falloffExp;

	// Color: normalize the 0–255 channels into [0,1]
	constexpr float inv255 = 1.0f / 255.0f;
	ambientColor.r = lightForm->red * inv255;
	ambientColor.g = lightForm->green * inv255;
	ambientColor.b = lightForm->blue * inv255;
	//ambientColor.a = 1.0f;

	// Use the same for diffuse
	diffuseColor = ambientColor;

	// Keep a back‑pointer so other code can reference the form
	baseLight = lightForm;
}

void NiPointLight::initLightBlock(NiNode* rootNode) {

	NiExtraData** list = this->m_extraDataList;
	int size = this->m_extraDataListLen;

	while (--size >= 0) {

		NiExtraData* extraData = list[size];
		if (!extraData) continue;

		if (extraData->name == s_LIGH_EDID()) {

			if (extraData->IsType(kVtbl_NiStringExtraData)) {

				NiStringExtraData* extraString = static_cast<NiStringExtraData*>(extraData);
				if (const char* edid = extraString->strData.getPtr()) {

					if (auto* form = LookupFormByEDID(edid); form && form->typeID == kFormType_TESObjectLIGH)
					{
						rootNode->m_flags.set(NiAVObject::kNiFlag_IsPointLight);
						this->m_flags.set(NiAVObject::kNiFlag_IsPointLight);
						this->baseLight = (TESObjectLIGH*)form;

						for (NiAVObject* parent = this->m_parent; parent && parent != rootNode; parent = parent->m_parent) {

							if (parent->m_flags & NiAVObject::kNiFlag_IsPointLight)
								break;

							parent->m_flags.set(NiAVObject::kNiFlag_IsPointLight);
						}

					}

				}
			}

			this->RemoveExtraData(size);

		}
	}

}

void NiNode::AddPointLights()
{
	// Fast‑out if no children
	const auto& children = this->m_children;
	size_t count = children.firstFreeEntry;
	if (count == 0)
		return;

	// Iterate through the raw child pointer array
	NiAVObject** ptr = children.data;
	for (size_t i = 0; i < count; ++i, ++ptr) {
		NiAVObject* child = *ptr;
		if (!child)
			continue;

		// Only consider nodes already flagged as “point‑light containers”
		if (!(child->m_flags & NiAVObject::kNiFlag_IsPointLight))
			continue;

		// If it’s actually a NiPointLight, initialize it
		if (child->IsType(kVtbl_NiPointLight)) {

			auto* light = static_cast<NiPointLight*>(child);

			// only once per light
			if (!(light->m_flags & NiAVObject::kNiFlag_DoneInitLights)) {
				// mark as done
				light->m_flags.set(NiAVObject::kNiFlag_DoneInitLights);

				// decide on/off based on its baseLight form‑flag
				if (light->baseLight && (light->baseLight->jipFormFlags6 & TESObjectLIGH::kFlag_OffByDefault) == 0)
				{
					light->m_flags.set(NiAVObject::kNiFlag_DisplayObject);
				}

				// pass the form pointer into SetLightProperties
				light->SetLightProperties(light->baseLight);

				// push into the global list of active point‑lights
				s_activePtLights->Append(light);
			}
		}
		// otherwise, if it’s a node, recurse
		else if (child->isNiNode()) {
			((NiNode*)child)->AddPointLights();
		}
	}
}

void NiNode::AddSuffixToAllChildren(const char* suffix) {
	if (!suffix || !*suffix)
		return;

	// Precompute once
	const size_t suffixLen = std::strlen(suffix);
	std::string newName;
	newName.reserve(64 + suffixLen);  // heuristic reserve

	// iterative depth‑first
	std::vector<NiAVObject*> stack;
	stack.reserve(64);
	stack.push_back(this);

	while (!stack.empty()) {
		NiAVObject* obj = stack.back();
		stack.pop_back();

		// rename in-place using our reusable buffer
		const char* orig = obj->m_blockName.CStr();
		size_t origLen = std::strlen(orig);
		newName.assign(orig, origLen);
		newName.append(suffix, suffixLen);
		obj->m_blockName = newName.c_str();

		// visit children if this is a node
		if (auto* node = obj->GetNiNode()) {
			UInt16 count = node->m_children.firstFreeEntry;
			for (UInt16 i = 0; i < count; ++i) {
				if (NiAVObject* child = node->m_children[i]) {
					stack.push_back(child);
				}
			}
		}
	}
}

NiAVObject* NiNode::BuildNiPath(const char* blockPath, NiBlockPathBuilder& output)
{
	if (!blockPath || !*blockPath) {
		output.captureLastFrame(this);	//Push root node
		return this;
	}
	return BuildNiPath(NiBlockPathBase(blockPath), output);

}

NiAVObject* NiNode::BuildNiPath(const NiBlockPathView& blockPath, NiBlockPathBuilder& output)
{
	if (blockPath.size() == 0) {
		output.captureLastFrame(this);	//Push root node
		return this;
	}

	output.clear();

	// If first segment equals this node, we skip recording it twice; adjust segIdx accordingly
	UInt16 seg0 = (this->m_blockName == blockPath[0]) ? 1u : 0u;
	output.enterChildNode(this, seg0);

	// early out if we already have no segments left
	if (seg0 >= blockPath.size())
		return this;

	return BuildNiPathIter(blockPath, output);

}

//Path can be spares
NiAVObject* NiNode::BuildNiPathIter(const NiBlockPathView& path, NiBlockPathBuilder& output) const
{

	// While there’s still some node on our back‑track stack
	while (!output.empty()) {
		const NiNode* current = output.getCurrentNode();

		// Try the next child index
		UInt16 numChildren = current->m_children.firstFreeEntry;
		UInt16 childIndex = output.nextChild();

		if (childIndex < numChildren) {
			// Grab the child into a temporary smart‑ptr
			NiAVObject* child = current->m_children[childIndex];

			// If that slot was empty, just skip it
			if (!child)
				continue;

			UInt32 segment = output.getCurrentSegment();

			// Match on the name
			if (child->m_blockName == path[segment]) {
				++segment;
				// If that was the last segment, capture & return it
				if (segment >= path.size()) {
					output.captureLastFrame(static_cast<const NiNode*>(child));
					return child;
				}
			}

			// If it’s a sub‑node, descend into it
			if (NiNode* nodeChild = (NiNode*)child->GetNiNode()) {
				output.enterChildNode(nodeChild, segment);
			}
		}
		else {
			// No more children here → backtrack
			output.pop();
		}
	}

	// Nothing found → return an empty NiRefPtr
	return nullptr;
}

//From Plugins+
NiAVObject* TESObjectREFR::findNodeByName(GetRootNodeMask which, const char* blockName)
{

	auto search = [&](NiNode* root) -> NiAVObject* {
		if (!root) return root;
		if (!blockName || blockName[0] == '\0')
			return root;
		return root->DeepSearchBySparsePath(NiBlockPathBase(blockName));
		};

	if (IsPlayer()) {

		if (which == GetRootNodeMask::kNone) {
			which.assign(PlayerCharacter::getCameraState());
		}

		// 3rd-person
		if (which.hasAll(GetRootNodeMask::kThirdPerson)) {
			if (auto* root3 = renderState ? renderState->rootNode : nullptr) {
				if (auto* found = search(root3)) {
					if (which.hasAll(GetRootNodeMask::kFirstPerson) == false) {
						return found;
					}
				}

			}
		}
		// 1st-person
		if (which.hasAll(GetRootNodeMask::kFirstPerson)) {
			if (auto* root1 = static_cast<PlayerCharacter*>(this)->node1stPerson) {
				if (auto* found = search(root1)) {
					return found;
				}
			}
		}
		// nothing matched
		return nullptr;
	}

	// non-player
	return search(getRootNode());
}

//From Plugins+
NiAVObject* NiNode::DeepSearchBySparsePath(const NiBlockPathView& blockPath)
{
	NiBlockPathBuilder builder;
	return BuildNiPath(blockPath, builder);
}

// public entry – takes a “A\B\C” string
NiAVObject* NiNode::DeepSearchByPath(const char* blockPath, uint16_t& matchedDepth)
{
	if (!blockPath || !*blockPath)
		return 0;

	// split into segments
	NiBlockPathBase path(blockPath);
	if (path.size() == 0)
		return 0;

	return DeepSearchByPath(path, matchedDepth);
}

// public overload – takes a pre-split path, if this fails, it will return the size.

NiAVObject* NiNode::DeepSearchByPath(const NiBlockPathView& blockPath, uint16_t& matchedDepth)
{
	const uint16_t pathLen = static_cast<uint16_t>(blockPath.size());
	matchedDepth = 0;

	if (pathLen == 0) return nullptr;                     // invalid
	if (m_blockName != blockPath[0]) return nullptr;      // root segment mismatch

	// Root matches
	matchedDepth = 1;
	NiAVObject* result = this;
	if (pathLen == 1) return result;                      // single-seg path

	const NiNode* currentNode = this;
	uint16_t segIdx = 1;   // next segment to match
	uint16_t childIdx = 0;

	while (currentNode) {
		const UInt16 numChildren = currentNode->m_children.firstFreeEntry;

		if (childIdx < numChildren) {
			NiAVObject* child = currentNode->m_children[childIdx++];
			if (!child) continue;

			// segment match?
			if (child->m_blockName == blockPath[segIdx]) {
				result = child;
				matchedDepth = static_cast<uint16_t>(segIdx + 1);

				// full match achieved
				if (matchedDepth == pathLen)
					return result;

				// must descend for next segment; if not a node, path can't be completed
				if (child->isNiNode()) {
					currentNode = static_cast<const NiNode*>(child);
					++segIdx;
					childIdx = 0;
					continue;
				}

				// can't descend further; return partial match
				return result;
			}
			// else keep scanning siblings at this level
		}
		else {
			// Out of siblings at this level → no deeper match possible without backtracking.
			return result;  // partial match at current matchedDepth
		}
	}

	// Shouldn't be reached; return whatever deepest we recorded.
	return result;
}

NiNode* NiNode::findParentNode(const NiFixedString& parentName, NiNode* stopAt)
{
	if (!parentName.getPtr())
		return nullptr;

	NiNode* childNode = this;

	while (childNode && childNode != stopAt) {
		NiNode* parent = childNode->m_parent;
		if (!parent)
			return nullptr;

		if (parent->m_blockName == parentName)
			return parent;

		childNode = parent;
	}

	return nullptr;
}

NiAVObject* NiNode::DeepSearchByName(const char* blockName) {
	if (!blockName || blockName[0] == '\0') return nullptr;
	NiFixedString fixed(blockName);
	return this->DeepSearchByName(fixed);
}

NiAVObject* NiNode::DeepSearchByName(const NiFixedString& nameStr)
{

	if (!this || !nameStr.getPtr()) return nullptr;

	NiAVObject* result = nullptr;
	this->TraverseHorizontal64([&](NiAVObject* obj) {
		if (obj->m_blockName == nameStr) {
			result = obj;
			return false; // stop traversal on first match
		}
		return true; // keep going
	});

	return result;

}