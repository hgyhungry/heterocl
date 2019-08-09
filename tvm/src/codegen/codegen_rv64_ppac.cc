/*
    author Guyue Huang (gh424@cornell.edu)
 */
 
#include <tvm/build_module.h>
#include <tvm/ir_pass.h>
#include <vector>
#include <string>
#include <regex>
#include <fstream>
#include <sys/types.h>
#include "./codegen_rv64_ppac.h"
#include "./build_common.h"

namespace TVM {
namespace codegen {

void CodeGenRV64PPAC::AddFunction(LoweredFunc f, 
  str2tupleMap<std::string, Type> map_arg_type) {
  
  this->InitFuncState(f);
  // Register alloc buffer type
  for (const auto & kv : f->handle_data_type) {
    RegisterHandleType(kv.first.get(), kv.second.type());
  }
  // Write entry function name
  this->stream << "void " << f->name << "(";
  // Write arguments
  for (size_t i = 0; i < f->args.size(); ++i) {
    Var v = f->args[i];
    std::string vid = AllocVarID(v.get());
    if (i != 0) this->stream << ", ";
    if (map_arg_type.find(vid) == map_arg_type.end()) {
      LOG(WARNING) << vid << " type not found\n";
      PrintType(v.type(), this->stream);
      this->stream << ' ' << vid;
    }
    else {
      auto arg = map_arg_type[vid];
      PrintType(std::get<1>(arg), this->stream);
      this->stream << "*"; 
      this->stream << ' ' << std::get<0>(arg);
      //const BufferNode* buf = f->api_args[i].as<BufferNode>();
      /*
      if (v.type().is_handle() && buf) {
        var_shape_map_[buf->data.get()] = buf->shape;
        for (size_t i = 0; i < buf->shape.size(); i++) {
          this->stream << '[';
          this->PrintExpr(buf->shape[i], this->stream);
          this->stream << ']';
        } 
      }*/
      
    }
  }
  stream << ") {\n";
  int func_scope = this->BeginScope();
  //range_ = CollectIterRange(f->body);
  this->PrintStmt(f->body);
  this->EndScope(func_scope);
  this->PrintIndent();
  this->stream << "}\n\n";
}

/*
std::string CodeGenRV64PPAC::GetBufferRef(Type t, const Variable* buffer, Expr index) {
  std::ostringstream os;
  std::string vid = GetVarID(buffer);
  if (t.lanes() == 1) {
    bool is_scalar = (buf_length_map_.count(buffer) == 1 &&
        buf_length_map_[buffer] == 1);
    if (is_scalar) {
      os << vid;
    } else {     
      os << vid;
      //std::vector<Expr> indices = ExtractIndices(index, var_shape_map_[buffer], range_);
      
      os << '[';
      PrintExpr(index, os);
      os << ']';
      
    }
  }  
  return os.str();
}*/

void CodeGenRV64PPAC::VisitStmt_(const LetStmt* op) {
  std::string value = PrintExpr(op->value);
  // Skip the argument retrieving assign statement
  std::string vid = AllocVarID(op->var.get());
  if (op->var.type() != Handle() &&
      value.find("TVMArray") == std::string::npos &&
      value.find("arg") != 0) {
    PrintIndent();
    PrintType(op->var.type(), this->stream);
    this->stream << ' '
                 << vid
                 << " = " << value << ";\n";
  }
  PrintStmt(op->body);
}

void CodeGenRV64PPAC::VisitStmt_(const IfThenElse* op) {
  std::string cond = PrintExpr(op->condition);
  // Skip the buffer data checking
  if (std::regex_match(cond, std::regex("!\\((arg)(.+)(== NULL)\\)")))
      return ;
  PrintIndent();
  if (cond[0] == '(' && cond[cond.length() - 1] == ')') {
    stream << "if " << cond << " {\n";
  } else {
    stream << "if (" << cond << ") {\n";
  }
  int then_scope = BeginScope();
  PrintStmt(op->then_case);
  this->EndScope(then_scope);
  if (op->else_case.defined()) {
    PrintIndent();
    stream << "} else {\n";
    int else_scope = BeginScope();
    PrintStmt(op->else_case);
    this->EndScope(else_scope);
  }
  PrintIndent();
  stream << "}\n";
}

void CodeGenRV64PPAC::PrintType(Type t, std::ostream& os) {
  CHECK_EQ(t.lanes(), 1)
      << "do not yet support vector types";
  if (t.is_handle()) {
    os << "void*"; return;
  }
  if (t.is_float()) {
    if (t.bits() == 32) {
      os << "float"; return;
    }
    if (t.bits() == 64) {
      os << "double"; return;
    }
  } else if (t.is_uint()) {
    if (t.bits() == 1) {
      os << "int"; return;
    } else if (t.bits()<=32) {
      os << "uint32_t"; return;
    } else if (t.bits() <= 64) {
      os << "uint64_t"; return;
    }
    /*
    switch (t.bits()) {
      case 8: case 16: case 32: case 64: {
        os << "uint" << t.bits() << "_t"; return;
      }
      case 1: os << "int"; return;
    }*/
  } else if (t.is_int()) {
    if (t.bits() == 1) {
      os << "int"; return;
    } else if (t.bits()<=32) {
      os << "int32_t"; return;
    } else if (t.bits() <= 64) {
      os << "int64_t"; return;
    }
    /*
    switch (t.bits()) {
      case 8: case 16: case 32: case 64: {
        os << "int" << t.bits() << "_t"; return;
      }
      case 1: os << "int"; return;
    }*/
  } else if (t.is_ufixed() && t.fracs()==0 ) {
    if (t.bits() <= 8) {
      os << "uint8_t"; return;
    }
    else if (t.bits() <= 16) {
      os << "uint16_t"; return;
    }
    else if (t.bits() <= 32) {
      os << "uint32_t"; return;
    }
    else if (t.bits() <= 64) {
      os << "uint64_t"; return;
    }
    else {
      os << "uint64_t";
      LOG(WARNING) << "Casting type " << t << " to int64_t";
    }
  } else if (t.is_fixed() && t.fracs()==0 ) {
    if (t.bits() <= 8) {
      os << "int8_t"; return;
    }
    else if (t.bits() <= 16) {
      os << "int16_t"; return;
    }
    else if (t.bits() <= 32) {
      os << "int32_t"; return;
    }
    else if (t.bits() <= 64) {
      os << "int64_t"; return;
    }
    else {
      os << "int64_t";
      LOG(WARNING) << "Casting type " << t << " to int64_t";
    }
  }
  os << t;
  //LOG(FATAL) << "Cannot convert type " << t << " to C type";
}

} //codegen
} //TVM