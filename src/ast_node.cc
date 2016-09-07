#include "./ast_node.h"
#include <nan.h>
#include <tree_sitter/runtime.h>
#include <v8.h>
#include "./ast_node_array.h"
#include "./util.h"

namespace node_tree_sitter {

using namespace v8;

Nan::Persistent<Function> ASTNode::constructor;
Nan::Persistent<String> ASTNode::row_key;
Nan::Persistent<String> ASTNode::column_key;

void ASTNode::Init(Local<Object> exports) {
  Local<FunctionTemplate> tpl = Nan::New<FunctionTemplate>(New);
  tpl->SetClassName(Nan::New("ASTNode").ToLocalChecked());
  tpl->InstanceTemplate()->SetInternalFieldCount(1);

  GetterPair enum_getters[] = {
    {"startIndex", StartIndex},
    {"startPosition", StartPosition},
    {"endIndex", EndIndex},
    {"endPosition", EndPosition},
    {"type", Type},
    {"isNamed", IsNamed},
  };

  GetterPair non_enum_getters[] = {
    {"parent", Parent},
    {"children", Children},
    {"namedChildren", NamedChildren},
    {"nextSibling", NextSibling},
    {"nextNamedSibling", NextNamedSibling},
    {"previousSibling", PreviousSibling},
    {"previousNamedSibling", PreviousNamedSibling},
  };

  FunctionPair methods[] = {
    {"isValid", IsValid},
    {"toString", ToString},
    {"descendantForIndex", DescendantForIndex},
    {"namedDescendantForIndex", NamedDescendantForIndex},
    {"descendantForPosition", DescendantForPosition},
    {"namedDescendantForPosition", NamedDescendantForPosition},
  };

  for (size_t i = 0; i < sizeof(enum_getters) / sizeof(enum_getters[0]); i++)
    Nan::SetAccessor(
      tpl->InstanceTemplate(),
      Nan::New(enum_getters[i].name).ToLocalChecked(),
      enum_getters[i].callback);

  for (size_t i = 0; i < sizeof(non_enum_getters) / sizeof(non_enum_getters[0]); i++)
    Nan::SetAccessor(
      tpl->InstanceTemplate(),
      Nan::New(non_enum_getters[i].name).ToLocalChecked(),
      non_enum_getters[i].callback,
      0, Local<Value>(), DEFAULT, DontEnum);

  for (size_t i = 0; i < sizeof(methods) / sizeof(methods[0]); i++)
    Nan::SetPrototypeMethod(tpl, methods[i].name, methods[i].callback);

  constructor.Reset(Nan::Persistent<Function>(tpl->GetFunction()));
  row_key.Reset(Nan::Persistent<String>(Nan::New("row").ToLocalChecked()));
  column_key.Reset(Nan::Persistent<String>(Nan::New("column").ToLocalChecked()));
}

Local<Object> ASTNode::PointToJS(const TSPoint &point) {
  Local<Object> result = Nan::New<Object>();
  result->Set(Nan::New(row_key), Nan::New<Number>(point.row));
  result->Set(Nan::New(column_key), Nan::New<Number>(point.column));
  return result;
}

Nan::Maybe<TSPoint> ASTNode::PointFromJS(const Local<Value> &arg) {
  if (!arg->IsObject()) {
    Nan::ThrowTypeError("Point must be a {row, column} object");
    return Nan::Nothing<TSPoint>();
  }

  Local<Object> js_point = Local<Object>::Cast(arg);
  Local<Value> js_row = js_point->Get(Nan::New(row_key));
  Local<Value> js_column = js_point->Get(Nan::New(column_key));
  if (!js_row->IsNumber() || !js_column->IsNumber())
    return Nan::Nothing<TSPoint>();

  int row = js_row->Int32Value();
  int column = js_column->Int32Value();
  return Nan::Just<TSPoint>({(size_t)row, (size_t)column});
}

static Nan::Maybe<size_t> ByteIndexFromJSStringIndex(const Local<Value> &input) {
  if (!input->IsNumber()) {
    Nan::ThrowTypeError("Character index must be a number");
    return Nan::Nothing<size_t>();
  }
  return Nan::Just<size_t>(input->Int32Value() * 2);
}

ASTNode *ASTNode::Unwrap(const Local<Object> &object) {
  ASTNode *node = Nan::ObjectWrap::Unwrap<ASTNode>(object);
  if (node && node->node_.data)
    return node;
  else
    return NULL;
}

ASTNode *ASTNode::UnwrapValid(const Local<Object> &object) {
  ASTNode *node = Unwrap(object);
  if (node && node->parse_count_ == ts_document_parse_count(node->document_))
    return node;
  else
    return NULL;
}

ASTNode::ASTNode(TSNode node, TSDocument *document, size_t parse_count) :
  node_(node), document_(document), parse_count_(parse_count) {}

Local<Value> ASTNode::NewInstance(TSNode node, TSDocument *document, size_t parse_count) {
  Local<Object> instance = Nan::New(constructor)->NewInstance(0, NULL);
  (new ASTNode(node, document, parse_count))->Wrap(instance);
  return instance;
}

NAN_METHOD(ASTNode::New) {
  info.GetReturnValue().Set(Nan::Null());
}

NAN_METHOD(ASTNode::ToString) {
  ASTNode *node = UnwrapValid(info.This());
  if (node) {
    const char *string = ts_node_string(node->node_, node->document_);
    info.GetReturnValue().Set(Nan::New(string).ToLocalChecked());
    free((char *)string);
  }
}

NAN_METHOD(ASTNode::IsValid) {
  ASTNode *node = Unwrap(info.This());
  if (node) {
    bool result = node->parse_count_ == ts_document_parse_count(node->document_);
    info.GetReturnValue().Set(Nan::New<Boolean>(result));
  }
}

NAN_METHOD(ASTNode::NamedDescendantForIndex) {
  ASTNode *node = UnwrapValid(info.This());
  if (node) {
    size_t min, max;
    switch (info.Length()) {
      case 1: {
        Nan::Maybe<size_t> maybe_value = ByteIndexFromJSStringIndex(info[0]);
        if (maybe_value.IsNothing()) return;
        min = max = maybe_value.FromJust();
        break;
      }
      case 2: {
        Nan::Maybe<size_t> maybe_min = ByteIndexFromJSStringIndex(info[0]);
        Nan::Maybe<size_t> maybe_max = ByteIndexFromJSStringIndex(info[1]);
        if (maybe_min.IsNothing()) return;
        if (maybe_max.IsNothing()) return;
        min = maybe_min.FromJust();
        max = maybe_max.FromJust();
        break;
      }
      default:
        Nan::ThrowTypeError("Must provide 1 or 2 character indices");
        return;
    }

    TSNode result = ts_node_named_descendant_for_byte_range(node->node_, min, max);
    info.GetReturnValue().Set(ASTNode::NewInstance(result, node->document_, node->parse_count_));
  }
}

NAN_METHOD(ASTNode::DescendantForIndex) {
  ASTNode *node = UnwrapValid(info.This());
  if (node) {
    size_t min, max;
    switch (info.Length()) {
      case 1: {
        Nan::Maybe<size_t> maybe_value = ByteIndexFromJSStringIndex(info[0]);
        if (maybe_value.IsNothing()) return;
        min = max = maybe_value.FromJust();
        break;
      }
      case 2: {
        Nan::Maybe<size_t> maybe_min = ByteIndexFromJSStringIndex(info[0]);
        Nan::Maybe<size_t> maybe_max = ByteIndexFromJSStringIndex(info[1]);
        if (maybe_min.IsNothing()) return;
        if (maybe_max.IsNothing()) return;
        min = maybe_min.FromJust();
        max = maybe_max.FromJust();
        break;
      }
      default:
        Nan::ThrowTypeError("Must provide 1 or 2 character indices");
        return;
    }

    TSNode result = ts_node_descendant_for_byte_range(node->node_, min, max);
    info.GetReturnValue().Set(ASTNode::NewInstance(result, node->document_, node->parse_count_));
  }
}

NAN_METHOD(ASTNode::NamedDescendantForPosition) {
  ASTNode *node = UnwrapValid(info.This());
  if (node) {
    TSPoint min, max;
    switch (info.Length()) {
      case 1: {
        Nan::Maybe<TSPoint> value = PointFromJS(info[0]);
        if (value.IsNothing()) return;
        min = max = value.FromJust();
        break;
      }
      case 2: {
        Nan::Maybe<TSPoint> maybe_min = PointFromJS(info[0]);
        Nan::Maybe<TSPoint> maybe_max = PointFromJS(info[1]);
        if (maybe_min.IsNothing()) return;
        if (maybe_max.IsNothing()) return;
        min = maybe_min.FromJust();
        max = maybe_max.FromJust();
        break;
      }
      default:
        Nan::ThrowTypeError("Must provide 1 or 2 points");
        return;
    }

    TSNode result = ts_node_named_descendant_for_point_range(node->node_, min, max);
    info.GetReturnValue().Set(ASTNode::NewInstance(result, node->document_, node->parse_count_));
  }
}

NAN_METHOD(ASTNode::DescendantForPosition) {
  ASTNode *node = UnwrapValid(info.This());
  if (node) {
    TSPoint min, max;
    switch (info.Length()) {
      case 1: {
        Nan::Maybe<TSPoint> value = PointFromJS(info[0]);
        if (value.IsNothing()) return;
        min = max = value.FromJust();
        break;
      }
      case 2: {
        Nan::Maybe<TSPoint> maybe_min = PointFromJS(info[0]);
        Nan::Maybe<TSPoint> maybe_max = PointFromJS(info[1]);
        if (maybe_min.IsNothing()) return;
        if (maybe_max.IsNothing()) return;
        min = maybe_min.FromJust();
        max = maybe_max.FromJust();
        break;
      }
      default:
        Nan::ThrowTypeError("Must provide 1 or 2 points");
        return;
    }

    TSNode result = ts_node_descendant_for_point_range(node->node_, min, max);
    info.GetReturnValue().Set(ASTNode::NewInstance(result, node->document_, node->parse_count_));
  }
}

NAN_GETTER(ASTNode::Type) {
  ASTNode *node = UnwrapValid(info.This());
  if (node) {
    const char *result = ts_node_type(node->node_, node->document_);
    info.GetReturnValue().Set(Nan::New(result).ToLocalChecked());
  } else {
    info.GetReturnValue().Set(Nan::Null());
  }
}

NAN_GETTER(ASTNode::IsNamed) {
  ASTNode *node = UnwrapValid(info.This());
  if (node) {
    bool result = ts_node_is_named(node->node_);
    info.GetReturnValue().Set(Nan::New(result));
  } else {
    info.GetReturnValue().Set(Nan::Null());
  }
}

NAN_GETTER(ASTNode::StartIndex) {
  ASTNode *node = UnwrapValid(info.This());
  if (node) {
    int32_t result = ts_node_start_byte(node->node_) / 2;
    info.GetReturnValue().Set(Nan::New<Integer>(result));
  } else {
    info.GetReturnValue().Set(Nan::Null());
  }
}

NAN_GETTER(ASTNode::EndIndex) {
  ASTNode *node = UnwrapValid(info.This());
  if (node) {
    int32_t result = ts_node_end_byte(node->node_) / 2;
    info.GetReturnValue().Set(Nan::New<Integer>(result));
  } else {
    info.GetReturnValue().Set(Nan::Null());
  }
}

NAN_GETTER(ASTNode::StartPosition) {
  ASTNode *node = UnwrapValid(info.This());
  if (node) {
    TSPoint result = ts_node_start_point(node->node_);
    info.GetReturnValue().Set(PointToJS(result));
  } else {
    info.GetReturnValue().Set(Nan::Null());
  }
}

NAN_GETTER(ASTNode::EndPosition) {
  ASTNode *node = UnwrapValid(info.This());
  if (node) {
    TSPoint result = ts_node_end_point(node->node_);
    info.GetReturnValue().Set(PointToJS(result));
  } else {
    info.GetReturnValue().Set(Nan::Null());
  }
}

NAN_GETTER(ASTNode::Children) {
  ASTNode *node = UnwrapValid(info.This());
  if (node)
    info.GetReturnValue().Set(ASTNodeArray::NewInstance(node->node_, node->document_, node->parse_count_, false));
  else
    info.GetReturnValue().Set(Nan::Null());
}

NAN_GETTER(ASTNode::NamedChildren) {
  ASTNode *node = UnwrapValid(info.This());
  if (node)
    info.GetReturnValue().Set(ASTNodeArray::NewInstance(node->node_, node->document_, node->parse_count_, true));
  else
    info.GetReturnValue().Set(Nan::Null());
}

NAN_GETTER(ASTNode::Parent) {
  ASTNode *node = UnwrapValid(info.This());
  if (node) {
    TSNode parent = ts_node_parent(node->node_);
    if (parent.data) {
      info.GetReturnValue().Set(ASTNode::NewInstance(parent, node->document_, node->parse_count_));
      return;
    }
  }
  info.GetReturnValue().Set(Nan::Null());
}

NAN_GETTER(ASTNode::NextSibling) {
  ASTNode *node = UnwrapValid(info.This());
  if (node) {
    TSNode sibling = ts_node_next_sibling(node->node_);
    if (sibling.data) {
      info.GetReturnValue().Set(ASTNode::NewInstance(sibling, node->document_, node->parse_count_));
      return;
    }
  }
  info.GetReturnValue().Set(Nan::Null());
}

NAN_GETTER(ASTNode::NextNamedSibling) {
  ASTNode *node = UnwrapValid(info.This());
  if (node) {
    TSNode sibling = ts_node_next_named_sibling(node->node_);
    if (sibling.data) {
      info.GetReturnValue().Set(ASTNode::NewInstance(sibling, node->document_, node->parse_count_));
      return;
    }
  }
  info.GetReturnValue().Set(Nan::Null());
}

NAN_GETTER(ASTNode::PreviousSibling) {
  ASTNode *node = UnwrapValid(info.This());
  if (node) {
    TSNode sibling = ts_node_prev_sibling(node->node_);
    if (sibling.data) {
      info.GetReturnValue().Set(ASTNode::NewInstance(sibling, node->document_, node->parse_count_));
      return;
    }
  }
  info.GetReturnValue().Set(Nan::Null());
}

NAN_GETTER(ASTNode::PreviousNamedSibling) {
  ASTNode *node = UnwrapValid(info.This());
  if (node) {
    TSNode sibling = ts_node_prev_named_sibling(node->node_);
    if (sibling.data) {
      info.GetReturnValue().Set(ASTNode::NewInstance(sibling, node->document_, node->parse_count_));
      return;
    }
  }
  info.GetReturnValue().Set(Nan::Null());
}

}  // namespace node_tree_sitter
