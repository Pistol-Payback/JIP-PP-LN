#include "internal/netimmerse.h"
#include "internal/jip_core.h"
#include "p_plus/FormModelHandler.hpp"

TempObject<NiFixedString> s_LIGH_EDID;

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

void NiPointLight::SetLightProperties(TESObjectLIGH* lightForm)
{
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
						rootNode->m_flags |= NiAVObject::kNiFlag_IsPointLight;
						this->m_flags |= NiAVObject::kNiFlag_IsPointLight;
						this->baseLight = (TESObjectLIGH*)form;

						for (NiAVObject* parent = this->m_parent; parent && parent != rootNode; parent = parent->m_parent) {

							if (parent->m_flags & NiAVObject::kNiFlag_IsPointLight)
								break;

							parent->m_flags |= NiAVObject::kNiFlag_IsPointLight;
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
				light->m_flags |= NiAVObject::kNiFlag_DoneInitLights;

				// decide on/off based on its baseLight form‑flag
				if (light->baseLight &&
					(light->baseLight->jipFormFlags6 & TESObjectLIGH::kFlag_OffByDefault) == 0)
				{
					light->m_flags |= NiAVObject::kNiFlag_DisplayObject;
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

	if (!suffix || !*suffix) return;

	// iterative depth-first with a small stack
	std::vector<NiAVObject*> stack;
	stack.reserve(64);
	stack.push_back(this);

	while (!stack.empty()) {
		NiAVObject* obj = stack.back();
		stack.pop_back();

		// rename
		std::string newName = obj->m_blockName.CStr();
		newName += suffix;
		obj->m_blockName = newName.c_str();

		// push children if this is a node
		if (NiNode* node = obj->GetNiNode()) {
			UInt16 num = node->m_children.firstFreeEntry;
			for (UInt16 i = 0; i < num; ++i) {
				if (NiAVObject* child = node->m_children[i])
					stack.push_back(child);
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

	return BuildNiPathIter(blockPath, output);

}

//Path can be spares
NiAVObject* NiNode::BuildNiPathIter(const NiBlockPathView& path, NiBlockPathBuilder& output) const
{

	while (!output.empty()) {

		const NiNode* current = output.getCurrentNode();

		// Try next child
		UInt16 numChildren = current->m_children.firstFreeEntry;
		UInt16 iter = output.nextChild();
		if (iter < numChildren) {

			if (NiAVObject* child = current->m_children[iter]) {

				UInt16 segment = output.getCurrentSegment();

				if (child->m_blockName == path[segment]) { //matches names

					segment += 1;
					if (segment >= path.size()) {
						output.captureLastFrame((NiNode*)child); //Throw the last segment on the stack
						return child;
					}

				}

				if (child->isNiNode()) {
					output.enterChildNode((NiNode*)child, segment);
				}

			}

		}
		else {
			// out of children -> backtrack
			output.pop();
		}
	}

	return nullptr;
}

//From Plugins+
NiAVObject* TESObjectREFR::findNodeByName(GetRootNodeMask which, const char* blockName)
{
	NiNode* root = nullptr;

	auto search = [&](NiNode* root) -> NiAVObject* {
		if (!root) return root;
		if (!blockName || !*blockName)
			return root;
		return root->DeepSearchBySparsePath(NiBlockPathBase(blockName));
		};

	if (IsPlayer()) {
		// 3rd-person
		if (which.has(GetRootNodeMask::kThirdPerson)) {
			if (auto* root3 = renderState ? renderState->rootNode : nullptr) {
				if (auto* found = search(root3))
					return found;
			}
		}
		// 1st-person
		if (which.has(GetRootNodeMask::kFirstPerson)) {
			if (auto* root1 = static_cast<PlayerCharacter*>(this)->node1stPerson) {
				if (auto* found = search(root1))
					return found;
			}
		}
		// nothing matched
		return nullptr;
	}

	// non-player
	return search(GetRootNode());

}

//From Plugins+
NiAVObject* NiNode::DeepSearchBySparsePath(const NiBlockPathView& blockPath)
{
	if (blockPath.size() == 0)
		return nullptr;
	// same logic as above
	NiBlockPathBuilder builder;
	return BuildNiPath(blockPath, builder);
}

// public entry – takes a “A\B\C” string
NiAVObject* NiNode::DeepSearchByPath(const char* blockPath)
{
	if (!blockPath || !*blockPath)
		return nullptr;

	// split into segments
	NiBlockPath path(blockPath);
	if (path.size() == 0)
		return nullptr;

	// if the first segment matches *this*, skip it; otherwise treat it as relative
	uint32_t startIdx = (this->m_blockName == path[0]) ? 1 : 0;
	return DeepSearchByPathIter(path, startIdx);
}

// public overload – takes a pre-split path
NiAVObject* NiNode::DeepSearchByPath(const NiBlockPathView& blockPath)
{
	if (blockPath.size() == 0)
		return nullptr;
	// same logic as above
	uint32_t startIdx = (this->m_blockName == blockPath[0]) ? 1 : 0;
	return DeepSearchByPathIter(blockPath, startIdx);
}

// private iterative core
NiAVObject* NiNode::DeepSearchByPathIter(const NiBlockPathView& blockPath, uint32_t startIdx)
{

	struct Frame {
		const NiNode* node;     // current node
		uint32_t       segIdx;   // which path segment we’re matching
		UInt16         childIdx; // next child to try
	};

	// early out if we already have no segments left
	if (startIdx >= blockPath.size())
		return this;

	std::vector<Frame> stack;
	stack.reserve(blockPath.size());
	stack.push_back({ this, startIdx, 0 });

	while (!stack.empty()) {
		Frame& frame = stack.back();
		const NiNode* cur = frame.node;
		uint32_t idx = frame.segIdx;

		UInt16 numChildren = cur->m_children.firstFreeEntry;
		if (frame.childIdx < numChildren) {
			NiAVObject* child = cur->m_children[frame.childIdx++];
			if (!child)
				continue;

			// match this segment?
			if (child->m_blockName == blockPath[idx]) {

				if (idx + 1 == blockPath.size())
					return child;

				if (NiNode* childNode = child->GetNiNode()) {
					stack.push_back({ childNode, idx + 1, 0 });
				}
			}
		}
		else {
			// no more children here -> backtrack
			stack.pop_back();
		}
	}

	return nullptr;
}

NiAVObject* NiNode::DeepSearchByName(const char* blockName) const
{

	if (!blockName || blockName[0] == '\0')
		return nullptr;

	NiFixedString fixedStr = NiFixedString(blockName);

	if (!fixedStr)
		return nullptr;

	if (this->m_blockName == fixedStr)
		return (NiAVObject*)this;
	else {
		return this->DeepSearchByNameIter(fixedStr);
	}

}

//Prevents name mangling with naked functions.
/*
NiAVObject* NiNode::GetBlockByName(const char* nameStr) const noexcept	//	str of NiFixedString
{
	return this->DeepSearchByName(nameStr);
}*/

NiAVObject* NiNode::DeepSearchByName(const NiFixedString& nameStr) const
{
	if (!nameStr.CStr() || !*nameStr.CStr())
		return nullptr;

	// check ourselves first
	if (m_blockName == nameStr)
		return const_cast<NiNode*>(this);

	return DeepSearchByNameIter(nameStr);
}


NiAVObject* NiNode::DeepSearchByNameIter(const NiFixedString& target) const
{

	struct Frame { const NiNode* node; UInt16 nextChild; };

	std::vector<Frame> stack;
	stack.reserve(64);
	stack.push_back({ this, 0 });

	while (!stack.empty()) {
		auto& fr = stack.back();
		const NiNode* cur = fr.node;

		// try next child
		if (fr.nextChild < cur->m_children.firstFreeEntry) {
			NiAVObject* child = cur->m_children[fr.nextChild++];
			if (!child)
				continue;

			// name match?
			if (child->m_blockName == target)
				return child;

			// if it’s a node, descend
			if (NiNode* childNode = child->GetNiNode()) {
				stack.push_back({ childNode, 0 });
			}
		}
		else {
			// exhausted all children -> backtrack
			stack.pop_back();
		}
	}

	return nullptr;


}