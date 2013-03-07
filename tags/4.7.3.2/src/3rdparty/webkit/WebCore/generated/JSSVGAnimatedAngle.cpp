/*
    This file is part of the WebKit open source project.
    This file has been generated by generate-bindings.pl. DO NOT MODIFY!

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Library General Public
    License as published by the Free Software Foundation; either
    version 2 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Library General Public License for more details.

    You should have received a copy of the GNU Library General Public License
    along with this library; see the file COPYING.LIB.  If not, write to
    the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
    Boston, MA 02110-1301, USA.
*/

#include "config.h"

#if ENABLE(SVG)

#include "JSSVGAnimatedAngle.h"

#include "JSSVGAngle.h"
#include <wtf/GetPtr.h>

using namespace JSC;

namespace WebCore {

ASSERT_CLASS_FITS_IN_CELL(JSSVGAnimatedAngle);

/* Hash table */

static const HashTableValue JSSVGAnimatedAngleTableValues[4] =
{
    { "baseVal", DontDelete|ReadOnly, (intptr_t)static_cast<PropertySlot::GetValueFunc>(jsSVGAnimatedAngleBaseVal), (intptr_t)0 },
    { "animVal", DontDelete|ReadOnly, (intptr_t)static_cast<PropertySlot::GetValueFunc>(jsSVGAnimatedAngleAnimVal), (intptr_t)0 },
    { "constructor", DontEnum|ReadOnly, (intptr_t)static_cast<PropertySlot::GetValueFunc>(jsSVGAnimatedAngleConstructor), (intptr_t)0 },
    { 0, 0, 0, 0 }
};

static JSC_CONST_HASHTABLE HashTable JSSVGAnimatedAngleTable =
#if ENABLE(PERFECT_HASH_SIZE)
    { 3, JSSVGAnimatedAngleTableValues, 0 };
#else
    { 8, 7, JSSVGAnimatedAngleTableValues, 0 };
#endif

/* Hash table for constructor */

static const HashTableValue JSSVGAnimatedAngleConstructorTableValues[1] =
{
    { 0, 0, 0, 0 }
};

static JSC_CONST_HASHTABLE HashTable JSSVGAnimatedAngleConstructorTable =
#if ENABLE(PERFECT_HASH_SIZE)
    { 0, JSSVGAnimatedAngleConstructorTableValues, 0 };
#else
    { 1, 0, JSSVGAnimatedAngleConstructorTableValues, 0 };
#endif

class JSSVGAnimatedAngleConstructor : public DOMConstructorObject {
public:
    JSSVGAnimatedAngleConstructor(ExecState* exec, JSDOMGlobalObject* globalObject)
        : DOMConstructorObject(JSSVGAnimatedAngleConstructor::createStructure(globalObject->objectPrototype()), globalObject)
    {
        putDirect(exec->propertyNames().prototype, JSSVGAnimatedAnglePrototype::self(exec, globalObject), None);
    }
    virtual bool getOwnPropertySlot(ExecState*, const Identifier&, PropertySlot&);
    virtual bool getOwnPropertyDescriptor(ExecState*, const Identifier&, PropertyDescriptor&);
    virtual const ClassInfo* classInfo() const { return &s_info; }
    static const ClassInfo s_info;

    static PassRefPtr<Structure> createStructure(JSValue proto) 
    { 
        return Structure::create(proto, TypeInfo(ObjectType, StructureFlags), AnonymousSlotCount); 
    }
    
protected:
    static const unsigned StructureFlags = OverridesGetOwnPropertySlot | ImplementsHasInstance | DOMConstructorObject::StructureFlags;
};

const ClassInfo JSSVGAnimatedAngleConstructor::s_info = { "SVGAnimatedAngleConstructor", 0, &JSSVGAnimatedAngleConstructorTable, 0 };

bool JSSVGAnimatedAngleConstructor::getOwnPropertySlot(ExecState* exec, const Identifier& propertyName, PropertySlot& slot)
{
    return getStaticValueSlot<JSSVGAnimatedAngleConstructor, DOMObject>(exec, &JSSVGAnimatedAngleConstructorTable, this, propertyName, slot);
}

bool JSSVGAnimatedAngleConstructor::getOwnPropertyDescriptor(ExecState* exec, const Identifier& propertyName, PropertyDescriptor& descriptor)
{
    return getStaticValueDescriptor<JSSVGAnimatedAngleConstructor, DOMObject>(exec, &JSSVGAnimatedAngleConstructorTable, this, propertyName, descriptor);
}

/* Hash table for prototype */

static const HashTableValue JSSVGAnimatedAnglePrototypeTableValues[1] =
{
    { 0, 0, 0, 0 }
};

static JSC_CONST_HASHTABLE HashTable JSSVGAnimatedAnglePrototypeTable =
#if ENABLE(PERFECT_HASH_SIZE)
    { 0, JSSVGAnimatedAnglePrototypeTableValues, 0 };
#else
    { 1, 0, JSSVGAnimatedAnglePrototypeTableValues, 0 };
#endif

const ClassInfo JSSVGAnimatedAnglePrototype::s_info = { "SVGAnimatedAnglePrototype", 0, &JSSVGAnimatedAnglePrototypeTable, 0 };

JSObject* JSSVGAnimatedAnglePrototype::self(ExecState* exec, JSGlobalObject* globalObject)
{
    return getDOMPrototype<JSSVGAnimatedAngle>(exec, globalObject);
}

const ClassInfo JSSVGAnimatedAngle::s_info = { "SVGAnimatedAngle", 0, &JSSVGAnimatedAngleTable, 0 };

JSSVGAnimatedAngle::JSSVGAnimatedAngle(NonNullPassRefPtr<Structure> structure, JSDOMGlobalObject* globalObject, PassRefPtr<SVGAnimatedAngle> impl)
    : DOMObjectWithGlobalPointer(structure, globalObject)
    , m_impl(impl)
{
}

JSSVGAnimatedAngle::~JSSVGAnimatedAngle()
{
    forgetDOMObject(this, impl());
    JSSVGContextCache::forgetWrapper(this);
}

JSObject* JSSVGAnimatedAngle::createPrototype(ExecState* exec, JSGlobalObject* globalObject)
{
    return new (exec) JSSVGAnimatedAnglePrototype(JSSVGAnimatedAnglePrototype::createStructure(globalObject->objectPrototype()));
}

bool JSSVGAnimatedAngle::getOwnPropertySlot(ExecState* exec, const Identifier& propertyName, PropertySlot& slot)
{
    return getStaticValueSlot<JSSVGAnimatedAngle, Base>(exec, &JSSVGAnimatedAngleTable, this, propertyName, slot);
}

bool JSSVGAnimatedAngle::getOwnPropertyDescriptor(ExecState* exec, const Identifier& propertyName, PropertyDescriptor& descriptor)
{
    return getStaticValueDescriptor<JSSVGAnimatedAngle, Base>(exec, &JSSVGAnimatedAngleTable, this, propertyName, descriptor);
}

JSValue jsSVGAnimatedAngleBaseVal(ExecState* exec, JSValue slotBase, const Identifier&)
{
    JSSVGAnimatedAngle* castedThis = static_cast<JSSVGAnimatedAngle*>(asObject(slotBase));
    UNUSED_PARAM(exec);
    SVGAnimatedAngle* imp = static_cast<SVGAnimatedAngle*>(castedThis->impl());
    JSValue result = toJS(exec, castedThis->globalObject(), JSSVGDynamicPODTypeWrapperCache<SVGAngle, SVGAnimatedAngle>::lookupOrCreateWrapper(imp, &SVGAnimatedAngle::baseVal, &SVGAnimatedAngle::setBaseVal).get(), JSSVGContextCache::svgContextForDOMObject(castedThis));;
    return result;
}

JSValue jsSVGAnimatedAngleAnimVal(ExecState* exec, JSValue slotBase, const Identifier&)
{
    JSSVGAnimatedAngle* castedThis = static_cast<JSSVGAnimatedAngle*>(asObject(slotBase));
    UNUSED_PARAM(exec);
    SVGAnimatedAngle* imp = static_cast<SVGAnimatedAngle*>(castedThis->impl());
    JSValue result = toJS(exec, castedThis->globalObject(), JSSVGDynamicPODTypeWrapperCache<SVGAngle, SVGAnimatedAngle>::lookupOrCreateWrapper(imp, &SVGAnimatedAngle::animVal, &SVGAnimatedAngle::setAnimVal).get(), JSSVGContextCache::svgContextForDOMObject(castedThis));;
    return result;
}

JSValue jsSVGAnimatedAngleConstructor(ExecState* exec, JSValue slotBase, const Identifier&)
{
    JSSVGAnimatedAngle* domObject = static_cast<JSSVGAnimatedAngle*>(asObject(slotBase));
    return JSSVGAnimatedAngle::getConstructor(exec, domObject->globalObject());
}
JSValue JSSVGAnimatedAngle::getConstructor(ExecState* exec, JSGlobalObject* globalObject)
{
    return getDOMConstructor<JSSVGAnimatedAngleConstructor>(exec, static_cast<JSDOMGlobalObject*>(globalObject));
}

JSC::JSValue toJS(JSC::ExecState* exec, JSDOMGlobalObject* globalObject, SVGAnimatedAngle* object, SVGElement* context)
{
    return getDOMObjectWrapper<JSSVGAnimatedAngle>(exec, globalObject, object, context);
}
SVGAnimatedAngle* toSVGAnimatedAngle(JSC::JSValue value)
{
    return value.inherits(&JSSVGAnimatedAngle::s_info) ? static_cast<JSSVGAnimatedAngle*>(asObject(value))->impl() : 0;
}

}

#endif // ENABLE(SVG)
