#pragma once
#include "SyncPosition.hpp"

SyncPosVector syncPosManager = {};

__declspec(noinline) void __stdcall SetRefrPositionHook_CPP(TESObjectREFR* updatingRef, const NiVector3* pos) {
	syncPosManager.syncReference(updatingRef, pos);
}

/*
extern "C" __declspec(noinline) uint32_t __stdcall
SetRefrPositionHook_CPP(
	TESObjectREFR* updatingRef,
	AlignedVector4*& pSrcPtrSlot,   // points at the caller's [ebp+8]
	float& pYawMem					// points at updatingRef->rotation.z
) {

	if (!s_syncPositionRef || !updatingRef || updatingRef->refID != 0x14) return 0;

	TESObjectCELL* pCell = updatingRef->parentCell;
	TESObjectCELL* sCell = s_syncPositionRef->parentCell;
	if (!pCell || !sCell) return 0;
	if (pCell != sCell) {
		TESWorldSpace* pw = pCell->worldSpace;
		if (!pw || sCell->worldSpace != pw) return 0;
	}

	// resolve node (fast-path NiFixedString compare)
	auto* renderState = s_syncPositionRef->renderState;
	if (!renderState || !renderState->rootNode) return 0;
	NiAVObject* node = renderState->rootNode;
	if (*s_syncPositionNode) {
		if (s_syncPositionNode->isValid() && node->m_blockName != s_syncPositionNode) {
			node = renderState->rootNode->DeepSearchByName(s_syncPositionNode);
			if (!node) return 0;
		}
	}

	*pSrcPtrSlot = node->WorldTranslate();
	uint32_t flags = 1;

	// optional yaw
	if (s_syncPositionFlags && pYawMem) {
		const NiMatrix33& WR = node->WorldRotate();
		const float y = WR.cr[0][0]; // m00
		const float x = WR.cr[0][1]; // m10
		pYawMem = ATan2(y, x);
		flags |= 2;
	}

	return flags;
}
*/
