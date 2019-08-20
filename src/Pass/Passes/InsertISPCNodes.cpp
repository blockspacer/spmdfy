#include <spmdfy/Pass/Passes/InsertISPCNodes.hpp>
#include <tuple>

namespace spmdfy {

namespace pass {

#define CASTAS(TYPE, NODE) dynamic_cast<TYPE>(NODE)

bool insertISPCNodes(SpmdTUTy &spmd_tu, clang::ASTContext &ast_context,
                     Workspace &workspace) {
    InsertISPCNode inserter(spmd_tu, ast_context, workspace);
    for (auto node : spmd_tu) {
        SPMDFY_INFO("Visiting Node {}", node->getNodeTypeName());
        if (node->getNodeType() == cfg::CFGNode::KernelFunc) {
            if (inserter.handleKernelFunc(
                    dynamic_cast<cfg::KernelFuncNode *>(node))) {
                SPMDFY_ERROR("Something is wrong");
                return true;
            }
        }
    }
    return false;
}

auto walkBackTill(cfg::CFGNode *node)
    -> std::tuple<cfg::CFGNode *, cfg::CFGNode::Node> {
    auto curr_node = node->getPrevious();
    while (true) {
        SPMDFY_INFO("Walking back");
        switch (curr_node->getNodeType()) {
        case cfg::CFGNode::Reconv:
            curr_node =
                CASTAS(cfg::ReconvNode *, curr_node)->getBack()->getPrevious();
            break;
        case cfg::CFGNode::IfStmt:
        case cfg::CFGNode::ForStmt:
        case cfg::CFGNode::ISPCBlock:
            return {curr_node, curr_node->getNodeType()};
        default:
            curr_node = curr_node->getPrevious();
        }
    }
    return {nullptr, cfg::CFGNode::Exit};
}

auto rmCFGNode(cfg::CFGNode * node) -> cfg::CFGNode *{
    // 1. get next and previous of current
    auto next = node->getNext();
    auto prev = node->getPrevious();

    // 2. Updating then nodes
    next->setPrevious(prev, cfg::CFGEdge::Complete);
    prev->setNext(next, cfg::CFGEdge::Complete);
    return node->getNext();
}

auto InsertISPCNode::handleKernelFunc(cfg::KernelFuncNode *kernel) -> bool {
    // 1. Inserting GridNode
    auto grid_start = new cfg::ISPCGridNode();
    kernel->splitEdge(grid_start);

    // 2. Inserting BlockNode
    auto block_start = new cfg::ISPCBlockNode();
    grid_start->splitEdge(block_start);

    // 3. Getting sync_node
    auto sync_node = m_workspace.syncthrds_queue.front();

    // 4. Getting back node
    auto [back_node, back_node_type] = walkBackTill(sync_node);
    SPMDFY_INFO("BlockNode : {}", back_node->getName());

    auto sync_block_end = new cfg::ISPCBlockExitNode();
    auto sync_block_start = new cfg::ISPCBlockNode();
    auto sync_replace = new cfg::ISPCBlockExitNode();
    auto prev_for = back_node->getPrevious();
    auto prev_if = back_node->getPrevious();
    switch (back_node_type) {
    case cfg::CFGNode::ISPCBlock:
        sync_node->splitEdge(sync_block_end);
        sync_block_end->splitEdge(sync_block_start);
        break;
    case cfg::CFGNode::ForStmt:
        prev_for->splitEdge(sync_block_end);
        back_node->splitEdge(sync_block_start);
        sync_node->splitEdge(sync_replace);
        break;
    case cfg::CFGNode::IfStmt:
        prev_if->splitEdge(sync_block_end);
        back_node->splitEdge(sync_block_start);
        sync_node->splitEdge(sync_replace);
        break;
    default:
        SPMDFY_ERROR("Wrong Back Node");
    }
    auto curr_node = rmCFGNode(sync_node);
    while (curr_node->getNodeType() != cfg::CFGNode::Exit) {
        curr_node = curr_node->getNext();
    }
    curr_node = curr_node->getPrevious();
    curr_node->splitEdge(new cfg::ISPCGridExitNode());
    return false;
}

} // namespace pass

} // namespace spmdfy