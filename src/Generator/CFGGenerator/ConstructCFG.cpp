#include <spmdfy/Generator/CFGGenerator/ConstructCFG.hpp>

namespace spmdfy {

#define DEF_CFG_VISITOR(NODE, BASE, PARAM)                                     \
    auto ConstructSpmdCFG::Visit##NODE##BASE(clang::NODE##BASE *PARAM)->bool

#define STMT_COUNT(STMT, STMT_CLASS)                                           \
    SPMDFY_INFO("s{} {} {}", m_stmt_count, STMT, STMT_CLASS);                  \
    m_stmt_count++;

DEF_CFG_VISITOR(Compound, Stmt, cpmd) {
    for (auto stmt : cpmd->body()) {
        if (!TraverseStmt(stmt))
            continue;
        STMT_COUNT(SRCDUMP(stmt), stmt->getStmtClassName());
    }
    return false;
}

DEF_CFG_VISITOR(Decl, Stmt, decl_stmt) {
    for (auto decl : decl_stmt->decls()) {
        STMT_COUNT(SRCDUMP(decl), decl_stmt->getStmtClassName());
        CFG::InternalNode *decl_node =
            new CFG::InternalNode(llvm::cast<const clang::VarDecl>(decl));
        splitEdge(decl_node);
    }
    return false;
}

DEF_CFG_VISITOR(For, Stmt, for_stmt) {
    STMT_COUNT(sourceDump(m_sm, m_lang_opts, for_stmt->getForLoc(),
                          for_stmt->getRParenLoc()), for_stmt->getStmtClassName());

    TraverseStmt(for_stmt->getBody());
    STMT_COUNT("Reconv }", "ReconvNode");

    return false;
}

DEF_CFG_VISITOR(If, Stmt, if_stmt) {
    STMT_COUNT(sourceDump(m_sm, m_lang_opts, if_stmt->getBeginLoc(),
                          if_stmt->getCond()->getEndLoc()), if_stmt->getStmtClassName());

    if (if_stmt->getThen())
        TraverseStmt(if_stmt->getThen());
    if (if_stmt->getElse())
        TraverseStmt(if_stmt->getElse());
    STMT_COUNT("Reconv }", "ReconvNode");
    return false;
}

DEF_CFG_VISITOR(Call, Expr, call) {
    STMT_COUNT(SRCDUMP(call), call->getStmtClassName());
    return false;
}

DEF_CFG_VISITOR(PseudoObject, Expr, pseudo) { return false; }

DEF_CFG_VISITOR(CompoundAssign, Operator, assgn) {
    STMT_COUNT(SRCDUMP(assgn), assgn->getStmtClassName());
    CFG::InternalNode *assgn_node = new CFG::InternalNode(assgn);
    splitEdge(assgn_node);
    return false;
}

DEF_CFG_VISITOR(Binary, Operator, binop) {
    STMT_COUNT(SRCDUMP(binop), binop->getStmtClassName());
    CFG::InternalNode *binop_node = new CFG::InternalNode(binop);
    splitEdge(binop_node);
    return false;
}

auto ConstructSpmdCFG::get() -> std::vector<CFG::CFGNode *> {
    return m_spmdfy_tutbl;
}

auto ConstructSpmdCFG::add(const clang::VarDecl *var_decl) -> bool {
    m_spmdfy_tutbl.push_back(new CFG::GlobalVarNode(var_decl));
    return true;
}

auto ConstructSpmdCFG::splitEdge(CFG::CFGNode *node) -> bool {
    SPMDFY_INFO("Adding node {}", node->getNodeTypeName());
    if (m_curr_node == nullptr) {
        SPMDFY_ERROR("Current node is null");
        return true;
    }
    SPMDFY_INFO("Edge splitting {}\n", m_curr_node->getNodeTypeName());
    auto next = m_curr_node->getNext();
    if (next == nullptr) {
        SPMDFY_ERROR("Current node is null");
        return true;
    }
    node->setNext(next, CFG::CFGEdge::Complete);
    m_curr_node->setNext(node, CFG::CFGEdge::Complete);
    next->setPrevious(node, CFG::CFGEdge::Complete);
    node->setPrevious(m_curr_node, CFG::CFGEdge::Complete);
    m_curr_node = node;
    return false;
}

auto ConstructSpmdCFG::add(const clang::FunctionDecl *func_decl) -> bool {
    auto func = new CFG::KernelFuncNode(func_decl);
    m_curr_node = func;
    auto func_exit = new CFG::ExitNode();
    func->setNext(func_exit, CFG::CFGEdge::Complete);
    STMT_COUNT("Entry", "EntryNode");
    TraverseStmt(func_decl->getBody());
    STMT_COUNT("Exit", "ExitNode");
    m_spmdfy_tutbl.push_back(func);
    return false;
}
auto ConstructSpmdCFG::add(const clang::CXXRecordDecl *record_decl) -> bool {
    m_cpp_tutbl.push_back(record_decl);
    return true;
}

} // namespace spmdfy