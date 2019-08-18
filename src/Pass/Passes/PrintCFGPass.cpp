#include <spmdfy/Pass/Passes/PrintCFGPass.hpp>

namespace spmdfy {

namespace pass {

bool print_cfg_pass(SpmdTUTy& spmd_tu, clang::ASTContext& ast_context, Workspace& workspace) {
    for (auto node : spmd_tu) {
        SPMDFY_INFO("Visiting Node {}", node->getNodeTypeName());
        if(node->getNodeType() == cfg::CFGNode::KernelFunc){
            cfg::CFGNode* curr_node = node;
            while(curr_node->getNodeType() != cfg::CFGNode::Exit){
                SPMDFY_INFO("Visiting Node {}", curr_node->getName());
                curr_node = curr_node->getNext();
            }
        }
    }
    return false;
}

} // namespace pass

} // namespace spmdfy