#include <spmdfy/Generator/CFGGenerator/CFGCodeGen.hpp>

namespace spmdfy {
namespace codegen {

auto CFGCodeGen::get() -> std::string const {
    SPMDFY_INFO("Generating Code\n");
    return traverseCFG();
}

auto CFGCodeGen::getFrom(cfg::CFGNode *) -> std::string const { return ""; }

auto CFGCodeGen::traverseCFG() -> std::string const {
    OStreamTy tu_gen;
    tu_gen << ispc_macros;
    for (auto node : m_node) {
        tu_gen << Visit(node);
    }
    return tu_gen.str();
}

// CodeGen Visitors

#define DEF_VISITOR(NODE, BASE, NAME)                                          \
    auto CFGCodeGen::Visit##NODE##BASE(const clang::NODE##BASE *NAME)          \
        ->std::string
#define DECL_DEF_VISITOR(NODE, NAME) DEF_VISITOR(NODE, Decl, NAME)
#define STMT_DEF_VISITOR(NODE, NAME) DEF_VISITOR(NODE, Stmt, NAME)
#define TYPE_DEF_VISITOR(NODE, NAME) DEF_VISITOR(NODE, Type, NAME)
#define CFGNODE_DEF_VISITOR(NODE, NAME)                                        \
    auto CFGCodeGen::Visit##NODE##Node(cfg::NODE##Node *NAME)->std::string

auto rmCastIf(const clang::Expr *expr) -> const clang::Expr * {
    if (llvm::isa<const clang::ImplicitCastExpr>(expr)) {
        return llvm::cast<const clang::ImplicitCastExpr>(expr)
            ->getSubExprAsWritten();
    }
    return expr;
}

std::string CFGCodeGen::getISPCBaseType(std::string from) {
    std::string to = from;
    if (g_SpmdfyTypeMap.find(from) != g_SpmdfyTypeMap.end()) {
        to = g_SpmdfyTypeMap.at(from);
    }
    SPMDFY_WARN("Converting from {} to {}", from, to);
    return to;
}

auto CFGCodeGen::VisitQualType(clang::QualType qual) -> std::string {
    SPMDFY_INFO("Visiting QualType: {}", qual.getAsString());
    OStreamTy qual_gen;

    if (qual.hasQualifiers()) {
        SPMDFY_INFO("Has Qualifier: {}", qual.getQualifiers().getAsString());
        qual_gen << qual.getQualifiers().getAsString() << " ";
        qual = qual.getUnqualifiedType().getDesugaredType(m_ast_context);
    }

    qual_gen << Visit(qual.getTypePtr());

    return qual_gen.str();
}

TYPE_DEF_VISITOR(Builtin, builtin) {
    clang::PrintingPolicy pm(m_lang_opts);
    SPMDFY_INFO("Visiting BuiltinType: {}", builtin->getName(pm).str());
    OStreamTy type_gen;
    type_gen << getISPCBaseType(builtin->getName(pm));
    return type_gen.str();
}

TYPE_DEF_VISITOR(IncompleteArray, incompl_array) {
    SPMDFY_INFO("Visiting IncompleteArrayType: {}",
                incompl_array->desugar().getAsString());
    OStreamTy incompl_array_gen;
    incompl_array_gen << incompl_array->getElementType().getAsString();
    return incompl_array_gen.str();
}

TYPE_DEF_VISITOR(Pointer, ptr) {
    SPMDFY_INFO("Visiting PointerType:");
    OStreamTy ptr_gen;
    ptr_gen << VisitQualType(ptr->getPointeeType()) << "*";
    return ptr_gen.str();
}

TYPE_DEF_VISITOR(Record, record) {
    SPMDFY_INFO("Visiting RecordType: {}",
                record->getDecl()->getNameAsString());
    OStreamTy record_gen;

    record_gen << record->getDecl()->getNameAsString();

    return record_gen.str();
}

DECL_DEF_VISITOR(ParmVar, param_decl) {
    SPMDFY_INFO("Visiting ParmVarDecl: {}", SRCDUMP(param_decl));
    OStreamTy param_gen;
    if (m_tu_context == cfg::CFGNode::Context::Kernel) {
        clang::QualType param_type = param_decl->getType();
        param_gen << "uniform ";
        if (param_type->isPointerType()) {
            param_gen << getISPCBaseType(
                             param_type->getPointeeType().getAsString())
                      << " ";
            param_gen << param_decl->getNameAsString() << "[]";
        } else {
            param_gen << VisitVarDecl(
                llvm::cast<const clang::VarDecl>(param_decl));
        }
        SPMDFY_WARN("SPMDFIED ParmVarDecl {}", param_gen.str());
    }
    return param_gen.str();
}

DECL_DEF_VISITOR(Var, var_decl) {
    SPMDFY_INFO("Visiting VarDecl: {}", SRCDUMP(var_decl));
    OStreamTy var_gen;
    if (m_tu_context == cfg::CFGNode::Context::Global) {
        var_gen << "const uniform ";
    } else if (var_decl->hasAttr<clang::CUDASharedAttr>()) {
        var_gen << "uniform ";
    }

    std::string var_name = var_decl->getNameAsString();
    clang::QualType type = var_decl->getType();
    std::string var_base_type = VisitQualType(type);

    if (type->isIncompleteType() &&
        var_decl->hasAttr<clang::CUDASharedAttr>()) {
        var_name = "* " + var_name;
        var_name +=
            " = uniform new uniform " + var_base_type + "[shared_memory_size]";
    } else if (type->isConstantArrayType()) {
        do {
            auto const_arr_type = clang::cast<clang::ConstantArrayType>(type);
            var_name =
                var_name + "[" +
                std::to_string((int)*const_arr_type->getSize().getRawData()) +
                "]";
            type = const_arr_type->getElementType();
        } while (type->isConstantArrayType());
        var_base_type = getISPCBaseType(type.getAsString());
    } else if (!type->isBuiltinType() && !type->isPointerType()) {
        SPMDFY_ERROR("Not Builtin Type: {}", type.getAsString());
        var_name = "&" + var_name;
    }

    if (const clang::Expr *initwc = var_decl->getInit(); (initwc)) {
        const clang::Expr *init = rmCastIf(initwc);
        std::string var_init = SRCDUMP(init);
        if (var_base_type.find("int8") != -1) {
            if (llvm::isa<const clang::CharacterLiteral>(init)) {
                var_init = std::to_string(
                    llvm::cast<const clang::CharacterLiteral>(init)
                        ->getValue());
            }
        }
        var_name += " = ";
        if (type->isPointerType() &&
            llvm::isa<clang::CXXNullPtrLiteralExpr>(init)) {
            var_init = "NULL";
        }
        if (!type->isBuiltinType()) {
            if (llvm::isa<const clang::CXXConstructExpr>(init)) {
                SPMDFY_INFO("Generating CXXConstructExpr");
                const clang::CXXConstructExpr *ctor_expr =
                    llvm::cast<const clang::CXXConstructExpr>(init);
                std::string ctor_type;
                std::vector<std::string> ctor_args;
                for (int i = 0; i < ctor_expr->getNumArgs(); i++) {
                    ctor_type +=
                        "_" + ctor_expr->getArg(i)->getType().getAsString();
                    ctor_args.push_back(SRCDUMP(ctor_expr->getArg(i)));
                }
                var_init = var_base_type + "_ctor" + ctor_type + "(";
                var_init += strJoin(ctor_args.begin(), ctor_args.end());
                var_init += ")";
            }
        }
        var_name += var_init;
    }

    var_gen << var_base_type << " " << var_name;
    return var_gen.str();
}

DECL_DEF_VISITOR(Function, func_decl) {
    SPMDFY_INFO("Visiting Function Decl {}", func_decl->getNameAsString());
    OStreamTy func_gen;

    if (m_tu_context == cfg::CFGNode::Kernel) {
        func_gen << "ISPC_KERNEL(" << func_decl->getNameAsString();
        auto params = func_decl->parameters();
        for (auto param : params) {
            func_gen << ", " << Visit(param);
        }
        func_gen << "){\n";
    }

    return func_gen.str();
}

CFGNODE_DEF_VISITOR(KernelFunc, kernel) {
    OStreamTy kernel_gen;
    m_tu_context = cfg::CFGNode::Context::Kernel;
    kernel_gen << Visit(kernel->getKernelNode());
    cfg::CFGNode *curr_node = kernel->getNext();
    while (curr_node->getNodeType() != cfg::CFGNode::Exit) {
        SPMDFY_INFO("Current Internal node: {}", curr_node->getName());
        kernel_gen << Visit(curr_node);
        curr_node = curr_node->getNext();
    }
    kernel_gen << "}\n";
    return kernel_gen.str();
}

CFGNODE_DEF_VISITOR(Internal, internal) {
    SPMDFY_INFO("CodeGen InternalNode {}", internal->getName());
    OStreamTy internal_gen;
    const std::string &node_name = internal->getInternalNodeName();
    if (node_name == "Var") {
        internal_gen << Visit(
                            internal->getInternalNodeAs<const clang::VarDecl>())
                     << ";\n";
    } else {
        internal_gen << internal->getName() << ";\n";
    }

    return internal_gen.str();
}

CFGNODE_DEF_VISITOR(ISPCBlock, ispc_block){
    SPMDFY_INFO("CodeGen ISPCBlock Node");
    return "ISPC_BLOCK_START\n";
}

CFGNODE_DEF_VISITOR(ISPCBlockExit, ispc_block){
    SPMDFY_INFO("CodeGen ISPCBlockExit Node");
    return "ISPC_BLOCK_END\n";
}

CFGNODE_DEF_VISITOR(ISPCGrid, ispc_block){
    SPMDFY_INFO("CodeGen ISPCGrid Node");
    return "ISPC_GRID_START\n";
}

CFGNODE_DEF_VISITOR(ISPCGridExit, ispc_block){
    SPMDFY_INFO("CodeGen ISPCGridExit Node");
    return "ISPC_GRID_END\n";
}

} // namespace codegen
} // namespace spmdfy