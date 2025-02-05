#include "layer_fields.hpp"
#include "../gdal_common.hpp"
#include "../gdal_field_defn.hpp"
#include "../gdal_layer.hpp"

namespace node_gdal {

Nan::Persistent<FunctionTemplate> LayerFields::constructor;

void LayerFields::Initialize(Local<Object> target) {
  Nan::HandleScope scope;

  Local<FunctionTemplate> lcons = Nan::New<FunctionTemplate>(LayerFields::New);
  lcons->InstanceTemplate()->SetInternalFieldCount(1);
  lcons->SetClassName(Nan::New("LayerFields").ToLocalChecked());

  Nan::SetPrototypeMethod(lcons, "toString", toString);
  Nan::SetPrototypeMethod(lcons, "count", count);
  Nan::SetPrototypeMethod(lcons, "get", get);
  Nan::SetPrototypeMethod(lcons, "remove", remove);
  Nan::SetPrototypeMethod(lcons, "getNames", getNames);
  Nan::SetPrototypeMethod(lcons, "indexOf", indexOf);
  Nan::SetPrototypeMethod(lcons, "reorder", reorder);
  Nan::SetPrototypeMethod(lcons, "add", add);
  // Nan::SetPrototypeMethod(lcons, "alter", alter);

  ATTR_DONT_ENUM(lcons, "layer", layerGetter, READ_ONLY_SETTER);

  Nan::Set(target, Nan::New("LayerFields").ToLocalChecked(), Nan::GetFunction(lcons).ToLocalChecked());

  constructor.Reset(lcons);
}

LayerFields::LayerFields() : Nan::ObjectWrap() {
}

LayerFields::~LayerFields() {
}

/**
 * @class LayerFields
 */
NAN_METHOD(LayerFields::New) {

  if (!info.IsConstructCall()) {
    Nan::ThrowError("Cannot call constructor as function, you need to use 'new' keyword");
    return;
  }
  if (info[0]->IsExternal()) {
    Local<External> ext = info[0].As<External>();
    void *ptr = ext->Value();
    LayerFields *layer = static_cast<LayerFields *>(ptr);
    layer->Wrap(info.This());
    info.GetReturnValue().Set(info.This());
    return;
  } else {
    Nan::ThrowError("Cannot create LayerFields directly");
    return;
  }
}

Local<Value> LayerFields::New(Local<Value> layer_obj) {
  Nan::EscapableHandleScope scope;

  LayerFields *wrapped = new LayerFields();

  v8::Local<v8::Value> ext = Nan::New<External>(wrapped);
  v8::Local<v8::Object> obj =
    Nan::NewInstance(Nan::GetFunction(Nan::New(LayerFields::constructor)).ToLocalChecked(), 1, &ext).ToLocalChecked();
  Nan::SetPrivate(obj, Nan::New("parent_").ToLocalChecked(), layer_obj);

  return scope.Escape(obj);
}

NAN_METHOD(LayerFields::toString) {
  info.GetReturnValue().Set(Nan::New("LayerFields").ToLocalChecked());
}

/**
 * Returns the number of fields.
 *
 * @method count
 * @instance
 * @memberof LayerFields
 * @return {number}
 */
NAN_METHOD(LayerFields::count) {

  Local<Object> parent =
    Nan::GetPrivate(info.This(), Nan::New("parent_").ToLocalChecked()).ToLocalChecked().As<Object>();
  Layer *layer = Nan::ObjectWrap::Unwrap<Layer>(parent);
  if (!layer->isAlive()) {
    Nan::ThrowError("Layer object already destroyed");
    return;
  }

  OGRFeatureDefn *def = layer->get()->GetLayerDefn();
  if (!def) {
    Nan::ThrowError("Layer has no layer definition set");
    return;
  }

  info.GetReturnValue().Set(Nan::New<Integer>(def->GetFieldCount()));
}

/**
 * Find the index of field in the layer.
 *
 * @method indexOf
 * @instance
 * @memberof LayerFields
 * @param {string} field
 * @return {number} Field index, or -1 if the field doesn't exist
 */
NAN_METHOD(LayerFields::indexOf) {

  Local<Object> parent =
    Nan::GetPrivate(info.This(), Nan::New("parent_").ToLocalChecked()).ToLocalChecked().As<Object>();
  Layer *layer = Nan::ObjectWrap::Unwrap<Layer>(parent);
  if (!layer->isAlive()) {
    Nan::ThrowError("Layer object already destroyed");
    return;
  }

  OGRFeatureDefn *def = layer->get()->GetLayerDefn();
  if (!def) {
    Nan::ThrowError("Layer has no layer definition set");
    return;
  }

  std::string name("");
  NODE_ARG_STR(0, "field name", name);

  info.GetReturnValue().Set(Nan::New<Integer>(def->GetFieldIndex(name.c_str())));
}

/**
 * Returns a field definition.
 *
 * @throws Error
 * @method get
 * @instance
 * @memberof LayerFields
 * @param {string|number} field Field name or index (0-based)
 * @return {FieldDefn}
 */
NAN_METHOD(LayerFields::get) {

  Local<Object> parent =
    Nan::GetPrivate(info.This(), Nan::New("parent_").ToLocalChecked()).ToLocalChecked().As<Object>();
  Layer *layer = Nan::ObjectWrap::Unwrap<Layer>(parent);
  if (!layer->isAlive()) {
    Nan::ThrowError("Layer object already destroyed");
    return;
  }

  if (info.Length() < 1) {
    Nan::ThrowError("Field index or name must be given");
    return;
  }

  OGRFeatureDefn *def = layer->get()->GetLayerDefn();
  if (!def) {
    Nan::ThrowError("Layer has no layer definition set");
    return;
  }

  int field_index;
  ARG_FIELD_ID(0, def, field_index);

  auto r = def->GetFieldDefn(field_index);
  if (r == nullptr) {
    NODE_THROW_LAST_CPLERR;
    return;
  }
  info.GetReturnValue().Set(FieldDefn::New(r));
}

/**
 * Returns a list of field names.
 *
 * @throws Error
 * @method getNames
 * @instance
 * @memberof LayerFields
 * @return {string[]} List of strings.
 */
NAN_METHOD(LayerFields::getNames) {

  Local<Object> parent =
    Nan::GetPrivate(info.This(), Nan::New("parent_").ToLocalChecked()).ToLocalChecked().As<Object>();
  Layer *layer = Nan::ObjectWrap::Unwrap<Layer>(parent);
  if (!layer->isAlive()) {
    Nan::ThrowError("Layer object already destroyed");
    return;
  }

  OGRFeatureDefn *def = layer->get()->GetLayerDefn();
  if (!def) {
    Nan::ThrowError("Layer has no layer definition set");
    return;
  }

  int n = def->GetFieldCount();
  Local<Array> result = Nan::New<Array>(n);

  for (int i = 0; i < n; i++) {
    OGRFieldDefn *field_def = def->GetFieldDefn(i);
    Nan::Set(result, i, SafeString::New(field_def->GetNameRef()));
  }

  info.GetReturnValue().Set(result);
}

/**
 * Removes a field.
 *
 * @throws Error
 * @method remove
 * @instance
 * @memberof LayerFields
 * @param {string|number} field Field name or index (0-based)
 */
NAN_METHOD(LayerFields::remove) {

  Local<Object> parent =
    Nan::GetPrivate(info.This(), Nan::New("parent_").ToLocalChecked()).ToLocalChecked().As<Object>();
  Layer *layer = Nan::ObjectWrap::Unwrap<Layer>(parent);
  if (!layer->isAlive()) {
    Nan::ThrowError("Layer object already destroyed");
    return;
  }

  if (info.Length() < 1) {
    Nan::ThrowError("Field index or name must be given");
    return;
  }

  OGRFeatureDefn *def = layer->get()->GetLayerDefn();
  if (!def) {
    Nan::ThrowError("Layer has no layer definition set");
    return;
  }

  int field_index;
  ARG_FIELD_ID(0, def, field_index);

  int err = layer->get()->DeleteField(field_index);
  if (err) {
    NODE_THROW_OGRERR(err);
    return;
  }

  return;
}

/**
 * Adds field(s).
 *
 * @throws Error
 * @method add
 * @instance
 * @memberof LayerFields
 * @param {FieldDefn|FieldDefn[]} defs A field definition, or array of field
 * definitions.
 * @param {boolean} [approx=true]
 */
NAN_METHOD(LayerFields::add) {

  Local<Object> parent =
    Nan::GetPrivate(info.This(), Nan::New("parent_").ToLocalChecked()).ToLocalChecked().As<Object>();
  Layer *layer = Nan::ObjectWrap::Unwrap<Layer>(parent);
  if (!layer->isAlive()) {
    Nan::ThrowError("Layer object already destroyed");
    return;
  }
  if (info.Length() < 1) {
    Nan::ThrowError("field definition(s) must be given");
    return;
  }

  FieldDefn *field_def;
  int err;
  int approx = 1;
  NODE_ARG_BOOL_OPT(1, "approx", approx);

  if (info[0]->IsArray()) {
    Local<Array> array = info[0].As<Array>();
    int n = array->Length();
    for (int i = 0; i < n; i++) {
      Local<Value> element = Nan::Get(array, i).ToLocalChecked();
      if (IS_WRAPPED(element, FieldDefn)) {
        field_def = Nan::ObjectWrap::Unwrap<FieldDefn>(element.As<Object>());
        err = layer->get()->CreateField(field_def->get(), approx);
        if (err) {
          NODE_THROW_OGRERR(err);
          return;
        }
      } else {
        Nan::ThrowError("All array elements must be FieldDefn objects");
        return;
      }
    }
  } else if (IS_WRAPPED(info[0], FieldDefn)) {
    field_def = Nan::ObjectWrap::Unwrap<FieldDefn>(info[0].As<Object>());
    err = layer->get()->CreateField(field_def->get(), approx);
    if (err) {
      NODE_THROW_OGRERR(err);
      return;
    }
  } else {
    Nan::ThrowError("field definition(s) must be a FieldDefn object or array of FieldDefn objects");
    return;
  }

  return;
}

/**
 * Reorders fields.
 *
 * @example
 *
 * // reverse field order
 * layer.fields.reorder([2,1,0]);
 *
 * @throws Error
 * @method reorder
 * @instance
 * @memberof LayerFields
 * @param {number[]} map An array of new indexes (integers)
 */
NAN_METHOD(LayerFields::reorder) {

  Local<Object> parent =
    Nan::GetPrivate(info.This(), Nan::New("parent_").ToLocalChecked()).ToLocalChecked().As<Object>();
  Layer *layer = Nan::ObjectWrap::Unwrap<Layer>(parent);
  if (!layer->isAlive()) {
    Nan::ThrowError("Layer object already destroyed");
    return;
  }

  OGRFeatureDefn *def = layer->get()->GetLayerDefn();
  if (!def) {
    Nan::ThrowError("Layer has no layer definition set");
    return;
  }

  Local<Array> field_map = Nan::New<Array>(0);
  NODE_ARG_ARRAY(0, "field map", field_map);

  int n = def->GetFieldCount();
  OGRErr err = 0;

  if ((int)field_map->Length() != n) {
    Nan::ThrowError("Array length must match field count");
    return;
  }

  int *field_map_array = new int[n];

  for (int i = 0; i < n; i++) {
    Local<Value> val = Nan::Get(field_map, i).ToLocalChecked();
    if (!val->IsNumber()) {
      delete[] field_map_array;
      Nan::ThrowError("Array must only contain integers");
      return;
    }

    int key = Nan::To<int64_t>(val).ToChecked();
    if (key < 0 || key >= n) {
      delete[] field_map_array;
      Nan::ThrowError("Values must be between 0 and field count - 1");
      return;
    }

    field_map_array[i] = key;
  }

  err = layer->get()->ReorderFields(field_map_array);

  delete[] field_map_array;

  if (err) {
    NODE_THROW_OGRERR(err);
    return;
  }
  return;
}

/**
 * Parent layer
 *
 * @readonly
 * @kind member
 * @name layer
 * @instance
 * @memberof LayerFields
 * @type {Layer}
 */
NAN_GETTER(LayerFields::layerGetter) {
  info.GetReturnValue().Set(Nan::GetPrivate(info.This(), Nan::New("parent_").ToLocalChecked()).ToLocalChecked());
}

} // namespace node_gdal
