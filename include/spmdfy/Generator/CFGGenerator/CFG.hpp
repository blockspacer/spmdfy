#ifndef SPMDFY_CFG_HPP
#define SPMDFY_CFG_HPP

#include <clang/AST/RecursiveASTVisitor.h>

#include <spmdfy/Logger.hpp>

#include <array>
#include <memory>
#include <tuple>
#include <variant>
#include <cassert>

template <class... Ts> struct visitor : Ts... { using Ts::operator()...; };
template <class... Ts> visitor(Ts...)->visitor<Ts...>;

namespace spmdfy {

namespace CFG {

class CFGNode;

class CFGEdge {
  public:
    enum Edge { Partial, Complete };

    // getters
    auto getEdgeType() -> Edge const { return m_edge; }
    auto getEdgeTypeName() -> std::string const;
    auto getTerminal() -> CFGNode *const;

    // setters
    auto setTerminal(CFGNode *terminal, Edge edge_type) -> bool;
    auto setEdgeType(Edge edge_type) -> bool;

  private:
    Edge m_edge;
    CFGNode *m_terminal;
};

class CFGNode {
  public:
    virtual ~CFGNode() = default;
    enum Node {
        GlobalVar,
        StructDecl,
        KernelFunc,
        DeviceFunc,
        IfStmt,
        ForStmt,
        Internal,
        Exit
    };
    enum Context { Global, Kernel, Device };
    // getters
    virtual auto getNodeType() -> Node const { return m_node_type; }
    virtual auto getContextType() -> Context const { return m_context; }
    auto getNodeTypeName() -> std::string const;
    auto getContextTypeName() -> std::string const;

    // virtual methods
    virtual auto getNext() -> CFGNode *const {
        SPMDFY_ERROR("Not supported operation for {}", getNodeTypeName());
        return nullptr;
    }
    virtual auto getPrevious() -> CFGNode *const {
        SPMDFY_ERROR("Not supported operation for {}", getNodeTypeName());
        return nullptr;
    }
    virtual auto setNext(CFGNode *node, CFGEdge::Edge edge_type) -> bool {
        SPMDFY_ERROR("Not supported operation for {}", getNodeTypeName());
        return true;
    }
    virtual auto setPrevious(CFGNode *node, CFGEdge::Edge edge_type) -> bool {
        SPMDFY_ERROR("Not supported operation for {}", getNodeTypeName());
        return true;
    }

  protected:
    Context m_context;
    Node m_node_type;
};

class GlobalVarNode : public CFGNode {
  public:
    GlobalVarNode(const clang::VarDecl *var_decl) {
        SPMDFY_INFO("Creating GlobalVarNode {}", var_decl->getNameAsString());
        m_var_decl = var_decl;
        m_node_type = GlobalVar;
        m_context = Global;
    }

    auto getDeclKindString() -> std::string const {
        return m_var_decl->getDeclKindName();
    }

  private:
    const clang::VarDecl *m_var_decl;
};

class KernelFuncNode : public CFGNode {
  public:
    KernelFuncNode(const clang::FunctionDecl *func_decl) {
        SPMDFY_INFO("Creating KerneFuncNode {}", func_decl->getNameAsString());
        m_func_decl = func_decl;
        m_node_type = KernelFunc;
        m_context = Global;
        next = new CFGEdge();
    }

    auto setNext(CFGNode *node, CFGEdge::Edge edge_type) -> bool override;
    auto getNext() -> CFGNode *const override;
    auto getDeclKindString() -> std::string const;

  private:
    const clang::FunctionDecl *m_func_decl;
    CFGEdge *next;
};

class InternalNode : public CFGNode {
  public:
    using NodeTy = std::variant<const clang::Decl *, const clang::Stmt *,
                                const clang::Expr *, const clang::Type *>;

    InternalNode(NodeTy node) : m_node(node) {
        SPMDFY_INFO("Creating InternalNode {}", getInternalNodeName());
        m_node_type = Internal;
        next = new CFGEdge();
        prev = new CFGEdge();
    }

    // getters
    auto getInternalNodeName() -> std::string const;

    // override
    auto getNext() -> CFGNode *const override;
    auto getPrevious() -> CFGNode *const override;
    auto setNext(CFGNode *node, CFGEdge::Edge edge_type) -> bool override;
    auto setPrevious(CFGNode *node, CFGEdge::Edge edge_type) -> bool override;

  private:
    NodeTy m_node;
    CFGEdge *next, *prev;
};

class ExitNode : public CFGNode {
  public:
    ExitNode() {
        SPMDFY_INFO("Creating ExitNode");
        m_node_type = Exit;
        prev = new CFGEdge();
    }
    auto getPrevious() -> CFGNode *const override;
    auto setPrevious(CFGNode *node, CFGEdge::Edge edge_type) -> bool override;

  private:
    CFGEdge *prev;
};

} // namespace CFG

} // namespace spmdfy

#endif