#pragma once
#include "internal/netimmerse.h"
#include "internal/jip_core.h"
#include "NiBlockPathBuilder.hpp"
#include <vector>
#include <unordered_map>
#include <utility>
#include <cstdint>

struct ChildBinding;

// ─────────────────────────────────────────────────────────────────────────────
// Parent bucket
// ─────────────────────────────────────────────────────────────────────────────
struct SyncNode {

    TESObjectREFR* parentRef = nullptr;
    std::vector<ChildBinding*> children;

    inline bool empty()  const noexcept { return children.empty(); }
    inline void clear() { children.clear(); }

    inline void setParent(TESObjectREFR* ref) { parentRef = ref; }

    inline bool remove(ChildBinding* item)
    {
        if (!item || children.empty()) return false;

        std::size_t idx = static_cast<std::size_t>(-1);
        for (std::size_t i = 0; i < children.size(); ++i) {
            if (children[i] == item) { idx = i; break; }  // compare pointers directly
        }
        if (idx == static_cast<std::size_t>(-1)) return false;

        const std::size_t last = children.size() - 1;
        if (idx != last) {
            std::swap(children[idx], children[last]);
        }
        children.pop_back();
        return true;
    }

};

// ─────────────────────────────────────────────────────────────────────────────
// Bind one CHILD to a cached node (on the CHILD)
// ─────────────────────────────────────────────────────────────────────────────
struct ChildBinding {

    enum {
        kIncludeRot         = BIT8(1),
        kChildController    = BIT8(2),
        kAutoClean          = BIT8(3),
        kCustomNode         = BIT8(4),
    };

    SyncNode*        parent = nullptr;   // owning bucket
    TESObjectREFR*   childRef = nullptr;

    NiVector3        modPosition = {};
    NiBlockPathBase  syncNodePath;

    pBitMask<UInt8> flags = {};

    // Copying would cause double-detach; forbid it.
    ChildBinding(const ChildBinding&) = delete;
    ChildBinding& operator=(const ChildBinding&) = delete;

    // Moving is allowed; leave the moved-from inert so its dtor does nothing.
    ChildBinding(ChildBinding&& other) noexcept : parent(other.parent), childRef(other.childRef), modPosition(other.modPosition), flags(other.flags), syncNodePath(other.syncNodePath)
    {
        other.clear();
    }

    // Safer to forbid move-assign; we don't need it for map node values.
    ChildBinding& operator=(ChildBinding&&) = delete;

    ChildBinding(SyncNode* parent, TESObjectREFR* childRef, NiVector3& modPos, pBitMask<UInt8> flags, NiBlockPathBase& path) :
        parent(parent), childRef(childRef), modPosition(modPos), flags(flags), syncNodePath(std::move(path))
    {
        if (isValid()) {
            syncChild(true, childRef->GetRefNiNode(), parent->parentRef->GetRefNiNode());
        }
        else {
            refresh3D(parent->parentRef->GetRefNiNode());
            refresh3D(childRef->GetRefNiNode());
        }
    }

    NiNode* getChildRefRoot() {
        if (childRef->IsPlayer() && PlayerCharacter::getCameraState() == GetRootNodeMask::kFirstPerson && g_thePlayer->node1stPerson) {
            return g_thePlayer->node1stPerson;
        }
        else {
            return childRef->GetRefNiNode();
        }
    }

    void syncChildFromParentMovement() {

        NiNode* syncNode = (NiNode*)extractSyncNode();
        NiNode* childNode = getChildRefRoot();

        bool inSameCell = childRef->GetInSameCellOrWorld(parent->parentRef);
        if (!inSameCell && Point2Distance(syncNode->WorldTranslate(), childRef->GetRefNiNode()->WorldTranslate()) >= 1024) {
            childRef->SetParentCell(parent->parentRef->GetParentCell());
        }
        syncChild(false, childNode, syncNode);

    }

    void syncFromChildMovement(const NiVector3& childInputPosition) {

        NiNode* syncNode = (NiNode*)extractSyncNode();
        NiNode* childNode = getChildRefRoot();

        if (flags.hasAll(kChildController) || childRef->IsPlayer()) {

            bool inSameCell = childRef->GetInSameCellOrWorld(parent->parentRef);
            if (!inSameCell && Point2Distance(syncNode->WorldTranslate(), childInputPosition) >= 1024) {
                childRef->position = childInputPosition;
                parent->parentRef->SetParentCell(childRef->GetParentCell());
                parent->parentRef->MoveToCell(childRef->GetParentCell(), childInputPosition); //Child moved cells
                return;
            }

        }
        syncChild(false, childNode, syncNode);

    }

    void syncChild(bool update3d, NiNode* childNode, NiNode* syncNode) {

        if (syncNode) {

            NiVector3 worldPos = syncNode->WorldTranslate() + modPosition;
            if (childRef->IsMobile()) {

                childRef->position = worldPos;
                childNode->WorldTranslate() = worldPos;
                //childNode->LocalTranslate() = worldPos;

                if (bhkCharacterController* charCtrl = childRef->GetCharacterController()) {
                    charCtrl->updatePosition(&worldPos);
                }

                if (flags.hasAll(kIncludeRot)) {

                    if (syncNode->hasChild(childNode) == false) {
                        syncNode->AddObject(childNode, true);
                        update3d = true;
                    }

                    childNode->WorldRotate() = syncNode->WorldRotate();
                    childNode->LocalRotate() = syncNode->WorldRotate();

                }
                else if (childRef->IsActor() == false) { //For moveable statics, they need to be added.

                    childNode->WorldRotate() = syncNode->WorldRotate();
                    childNode->LocalRotate() = syncNode->WorldRotate();

                    if (syncNode->hasChild(childNode) == false) {
                        syncNode->AddObject(childNode, true);
                        update3d = true;
                    }

                }

            }
            else {

                if (syncNode->hasChild(childNode) == false) {
                    syncNode->AddObject(childNode, true);
                }

                childRef->position = worldPos;
                childNode->LocalTranslate() = { 0, 0, 0 };  //Statics

                //if (flags.has(kIncludeRot)) {
                    //childNode->LocalRotate() = syncNode->WorldRotate();
                //}

            }

        }

        if (update3d) {
            NiNode* parentNode = parent->parentRef->GetRefNiNode();
            refresh3D(parentNode);
            refresh3D(childNode);
        }

    }

    void refresh3D(NiNode* node) {
        if (node) {
            node->ResetCollision();
            node->updatePalette();
            //node->UpdateTransformAndBounds(kNiUpdateData);
            node->UpdateDownwardPass(kNiUpdateData, 0);
            if (node->m_parent) {
                node->m_parent->UpdateUpwardPass();
            }
        }
    }

    void clear() {
        parent = nullptr;
        childRef = nullptr;
        modPosition = {};
        syncNodePath = {};
        flags = {};
    }

    ~ChildBinding() noexcept { }

    void restoreNodes(NiNode* syncNode) {

        if (childRef) {

            NiNode* childNode = childRef->GetRefNiNode();

            if (childNode) {

                TESForm* form = childRef ? childRef->baseForm : nullptr;
                const char* edid = (form && form->GetEditorID()) ? form->GetEditorID() : "(none)";
                UInt32      type = form ? static_cast<unsigned>(form->typeID) : 0xFFu;

                TESObjectCELL* cell = childRef ? childRef->GetParentCell() : nullptr;
                if (!cell) {
                    Console_Print("syncPos: ~ChildBinding — no valid cell (type=%u, editorID='%s').", type, edid);
                }
                else {
                    using CRD = TESObjectCELL::CellRenderData;
                    CRD::CellSubNodes idx = CRD::formToSubNodes(childRef);
                    if (idx == CRD::kNodeIdx_Null) {
                        Console_Print("syncPos: ~ChildBinding — no render sub-node for ref (type=%u, editorID='%s').", type, edid);
                    }
                    else {
                        NiNode* cellRoot = cell->Get3DNode(idx);
                        if (!cellRoot) {
                            Console_Print("syncPos: ~ChildBinding — cell->Get3DNode returned null (index=%d, type=%u, editorID='%s').", static_cast<int>(idx), type, edid);
                        }
                        else {
                            cellRoot->AddObject(childNode, true);
                        }
                    }
                }

            }

        }

        // Collapse temporary runtime node if we created one
        //if (flags.has(kCustomNode) && syncNode) {
            //syncNode->collapseRuntimeNode();
        //}

    }

    NiVector3 getSyncPosition(NiNode* syncNode) {
        if (syncNode) {
            return syncNode->WorldTranslate() + modPosition;
        }
        return childRef->position;
    }

    void finalizePosition(NiVector3& worldPos) {

        NiNode* childNode = childRef->GetRefNiNode();

        if (childRef->IsMobile()) {
            if (bhkCharacterController* charCtrl = childRef->GetCharacterController()) {
                charCtrl->updatePosition(&worldPos);
            }
        }

        childRef->SetPos(worldPos);
       // childRef->position = worldPos;
        //childNode->LocalTranslate() = worldPos;
        //childNode->WorldTranslate() = worldPos;

        if (!GetMenuMode()) {
            NiNode* parentNode = parent->parentRef->GetRefNiNode();
            refresh3D(parentNode);
            refresh3D(childNode);
            //childRef->Update3D();
        }

    }

    static NiAVObject* extractSyncNode(TESObjectREFR* parentRef, NiBlockPathBase& path) {

        auto* parentRender = parentRef->renderState;
        if (!parentRender || !parentRender->rootNode) return nullptr;

        NiBlockPathBuilder builder;
        return static_cast<NiAVObject*>(parentRender->rootNode->BuildNiPath(path, builder));

    }

    NiAVObject* extractSyncNode() {

        auto* parentRender = parent->parentRef->renderState;
        if (!parentRender || !parentRender->rootNode) return nullptr;

        NiBlockPathBuilder builder;
        return static_cast<NiAVObject*>(parentRender->rootNode->BuildNiPath(syncNodePath, builder));

    }

    bool isValid() {

        //|| !parent->parentRef->parentCell != TESObjectCELL::kState_Loaded || !childRef->parentCell != TESObjectCELL::kState_Loaded
        if (!parent || !parent->parentRef || !childRef || !parent->parentRef->parentCell) return false;

        auto* parentRender = parent->parentRef->renderState;
        if (!parentRender || !parentRender->rootNode) {
            return false;
        }

        auto* childRender = childRef->renderState;
        if (!childRender || !childRender->rootNode) {
            return false;
        }

        return true;
    }

    //Not used
    NiWeakPtr<NiNode> convertSyncBlock(NiAVObject* block) {

        if (!block) return {};

        if (block->isNiNode()) {
            return NiWeakPtr<NiNode>((NiNode*)block);
        }
        NiNode* grandparent = block->m_parent;
        NiNode* custom = createCustomSyncNode(block, grandparent);


        return custom;

    }

    /*  Not used 
    void setSyncBlock(NiAVObject* block) {

        if (!block) return;

        if (block->isNiNode()) {
            syncNode = (NiNode*)block;
            return;
        }

        NiNode* grandparent = block->m_parent;
        syncNode = createCustomSyncNode(block, grandparent);

    }
    */
    inline static UInt32 tempBlockCounter = 0;
    inline NiNode* createCustomSyncNode(NiAVObject* insertChild, NiNode* parent) {

        flags.set(kCustomNode);
        const std::string name = "JIP_SyncBlockTemp" + std::to_string(tempBlockCounter++);
        NiNode* custom = NiNode::nCreate(name.c_str());
        custom->AddObject(insertChild, true);
        parent->AddObject(custom, true);

        return custom;

    }

    inline SyncNode* getParent() const { return parent; }

};

// ─────────────────────────────────────────────────────────────────────────────
// Manager: single-parent enforcement, reparenting, hot-path sync
// ─────────────────────────────────────────────────────────────────────────────
struct SyncPosVector {

    std::unordered_map<TESObjectREFR*, SyncNode>      parentBuckets;
    std::unordered_map<TESObjectREFR*, ChildBinding>  childStorage;

    //Called from game load, does not finalize position.
    void clear() {

        for (auto it = parentBuckets.begin(); it != parentBuckets.end(); ) {

            SyncNode& bucket = it->second;

            for (ChildBinding* binding : bucket.children) {
                if (!binding) continue;

                NiNode* syncNode = (NiNode*)binding->extractSyncNode();
                binding->restoreNodes(syncNode);

                TESObjectREFR* child = binding->childRef;
                if (child) clearSyncFlagOn(child);

                if (child) {
                    childStorage.erase(child);
                }
            }

            TESObjectREFR* parentRef = it->first;
            if (parentRef) clearSyncFlagOn(parentRef);

            it = parentBuckets.erase(it);

        }

    }

    // --- render-state flag helpers (toggle only our known bit) ----------------
    static inline void setSyncFlagOn(TESObjectREFR* ref) {
        if (!ref) return;
        if (auto* render = ref->renderState) render->setSyncFlag();
    }
    static inline void clearSyncFlagOn(TESObjectREFR* ref) {
        if (!ref) return;
        if (auto* render = ref->renderState) render->removeSyncFlag();
    }
    static inline bool hasSyncFlagOn(TESObjectREFR* ref) {
        if (!ref) return false;
        if (auto* render = ref->renderState) return render->hasSyncFlag();
        return false;
    }

    inline ChildBinding* getIsChildBinding(TESObjectREFR* childRef) {
        auto it = childStorage.find(childRef);
        return (it != childStorage.end()) ? &it->second : nullptr;
    }

    inline SyncNode* getIsParent(TESObjectREFR* parent) {
        auto it = parentBuckets.find(parent);
        return (it != parentBuckets.end()) ? &it->second : nullptr;
    }


    inline ChildBinding* insertBinding(TESObjectREFR* parentRef, TESObjectREFR* childRef, NiAVObject* newCachedNode, NiVector3& modPos, pBitMask<UInt8> flags, NiBlockPathBase& attachPath) {

        if (!parentRef || !childRef) return nullptr;

        SyncNode& bucket = parentBuckets[parentRef];
        const bool wasEmpty = bucket.children.empty();
        if (!bucket.parentRef) bucket.setParent(parentRef);

        auto [it, inserted] = childStorage.try_emplace(childRef, &bucket, childRef, modPos, flags, attachPath);

        ChildBinding& binding = it->second;
        if (!inserted) {
            Console_Print("JIP Error, insertBinding tried to insert a dupe childRef");
            return nullptr;
        }

        // Point the bucket at the map-stored binding
        bucket.children.push_back(&binding);

        if (wasEmpty) setSyncFlagOn(parentRef); // first child → flag parent
        setSyncFlagOn(childRef);                // always flag child

        return &binding;

    }

    // Reassign existing child to a new parent / node / yaw setting
    void reassignParent(TESObjectREFR* newParent, NiAVObject* newCachedNode, NiVector3& modPos, pBitMask<UInt8> newFlags, NiBlockPathBase& attachPath, ChildBinding* oldBinding) {

        if (!newParent || !oldBinding) return;

        SyncNode* oldParent = oldBinding->getParent();
        if (!oldParent) return;

        if (oldParent->parentRef == newParent) {
            // Same bucket; just retarget the node + flags
            oldBinding->flags.clear();
            oldBinding->syncNodePath = std::move(attachPath);
            //oldBinding->setSyncBlock(newCachedNode);
            oldBinding->flags.set(newFlags);
            oldBinding->modPosition = modPos;

            setSyncFlagOn(oldBinding->childRef);
            setSyncFlagOn(newParent);
            return;
        }

        // Move the pointer between buckets

            SyncNode& newBucket = parentBuckets[newParent];
            newBucket.setParent(newParent);

            // Remove pointer from old bucket
            oldParent->remove(oldBinding);

            // Attach to new bucket
            oldBinding->parent = &newBucket;
            oldBinding->flags.clear();
            oldBinding->syncNodePath = std::move(attachPath);
            //oldBinding->setSyncBlock(newCachedNode);
            oldBinding->flags.set(newFlags);
            oldBinding->modPosition = modPos;

            newBucket.children.push_back(oldBinding);

            setSyncFlagOn(oldBinding->childRef);
            setSyncFlagOn(newParent);

            if (oldParent->empty()) {
                parentBuckets.erase(oldParent->parentRef);
                clearSyncFlagOn(oldParent->parentRef);
            }

    }

    bool attachRef(TESObjectREFR* parentRef, TESObjectREFR* childRef, NiBlockPathBase& attachPath, NiVector3& modPos, pBitMask<UInt8> newFlags) {

        if (!parentRef || !childRef) return false;

        if (childRef->GetDistance(parentRef) >= 1024) {
            TESObjectCELL* parentCell = parentRef->GetParentCell();
            childRef->MoveToCell(parentCell, parentRef->position);
        }

        NiAVObject* targetNode = nullptr;
        //NiAVObject* targetNode = ChildBinding::extractSyncNode(parentRef, attachPath);
        //if (newFlags.has(ChildBinding::kAutoClean) && !targetNode) {
            //return false;
        //}

        // Already tracked? Reassign.
        if (ChildBinding* existing = getIsChildBinding(childRef)) {
            reassignParent(parentRef, targetNode, modPos, newFlags, attachPath, existing);
            return true;
        }

        // New binding
        ChildBinding* newBinding = insertBinding(parentRef, childRef, targetNode, modPos, newFlags, attachPath);
        return true;

    }

    bool detachChild(TESObjectREFR* childRef) {

        if (!childRef) return false;

        if (ChildBinding* binding = getIsChildBinding(childRef)) {
            // Clear child's flag first
            clearSyncFlagOn(childRef);

            // Remove pointer from its parent bucket
            SyncNode* parent = binding->getParent();
            if (parent) {
                parent->remove(binding);
            }

            NiNode* syncNode = (NiNode*)binding->extractSyncNode();
            NiVector3 finalPosition = binding->getSyncPosition(syncNode);
            binding->restoreNodes(syncNode);
            binding->finalizePosition(finalPosition);
            // Erase from map (runs ~ChildBinding to restore scene/collapse node)
            childStorage.erase(childRef);

            // If parent bucket exists and is empty, clear flag and erase bucket
            if (parent && parent->empty()) {
                parentBuckets.erase(parent->parentRef);
                clearSyncFlagOn(parent->parentRef);
            }
            return true;

        }
        return false;

    }

    void detachAllForParent(TESObjectREFR* parentRef) {

        auto it = parentBuckets.find(parentRef);
        if (it == parentBuckets.end()) return;

        SyncNode& bucket = it->second;

        // Clear each child's flag and erase its binding from the map (runs destructor)
        for (ChildBinding* binding : bucket.children) {
            if (binding && binding->childRef) {

                NiNode* syncNode = (NiNode*)binding->extractSyncNode();
                NiVector3 finalPosition = binding->getSyncPosition(syncNode);
                binding->restoreNodes(syncNode);
                binding->finalizePosition(finalPosition);

                clearSyncFlagOn(binding->childRef);
                childStorage.erase(binding->childRef);
            }
        }

        // Clear the bucket’s pointer list then remove the bucket and clear parent flag
        bucket.children.clear();
        parentBuckets.erase(it);
        clearSyncFlagOn(parentRef);

    }

    inline bool UpdateChild(ChildBinding* child, const NiVector3& inputPos) {

        if (child->isValid()) {
            child->syncFromChildMovement(inputPos);
            return true;
        }
        else if (child->flags.hasAll(ChildBinding::kAutoClean)) {
            detachChild(child->childRef);
        }
        return false;
    }

    inline void UpdateAllChildren(SyncNode* syncNode) {
        for (auto child : syncNode->children) {
            if (child->isValid()) {
                child->syncChildFromParentMovement();
            }
            else if (child->flags.hasAll(ChildBinding::kAutoClean)) {
                detachChild(child->childRef);
            }
        }
    }

    // HOT PATH: choose first valid child when updatingRef is a parent,
    // or zero out when updatingRef is a child.
    void syncReference(TESObjectREFR* updatingRef, const NiVector3* pos) {

        if (!updatingRef) return;

        if (hasSyncFlagOn(updatingRef)) {

            if (ChildBinding* binding = getIsChildBinding(updatingRef)) {
                if (!UpdateChild(binding, *pos)) {
                    updatingRef->position = *pos;
                }
                return;
            }

            if (SyncNode* node = getIsParent(updatingRef)) {
                updatingRef->position = *pos;
                UpdateAllChildren(node);
                return;
            }

        }

        updatingRef->position = *pos;

    }

};

// Global manager instance
extern SyncPosVector syncPosManager;
