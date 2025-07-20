#pragma once
#include "FormModelHandler.hpp"

//From hooks.h
void DoAttachModels_New(TESForm* form, NiNode* rootNode) {

    auto runtimeNodesList = FormRuntimeModelManager::getSingleton().getNodesList(form);
    ModelBuilder(rootNode, runtimeNodesList);
    //ModelBuilder::checkRuntimeNodes(rootNode, runtimeNodesList);

}