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

#if ENABLE(SVG) && ENABLE(FILTERS)

#include "JSSVGFEDistantLightElement.h"

#include "JSSVGAnimatedNumber.h"
#include "SVGFEDistantLightElement.h"
#include <wtf/GetPtr.h>

using namespace JSC;

namespace WebCore {

ASSERT_CLASS_FITS_IN_CELL(JSSVGFEDistantLightElement);

/* Hash table */

static const HashTableValue JSSVGFEDistantLightElementTableValues[4] =
{
    { "azimuth", DontDelete|ReadOnly, (intptr_t)static_cast<PropertySlot::GetValueFunc>(jsSVGFEDistantLightElementAzimuth), (intptr_t)0 },
    { "elevation", DontDelete|ReadOnly, (intptr_t)static_cast<PropertySlot::GetValueFunc>(jsSVGFEDistantLightElementElevation), (intptr_t)0 },
    { "constructor", DontEnum|ReadOnly, (intptr_t)static_cast<PropertySlot::GetValueFunc>(jsSVGFEDistantLightElementConstructor), (intptr_t)0 },
    { 0, 0, 0, 0 }
};

static JSC_CONST_HASHTABLE HashTable JSSVGFEDistantLightElementTable =
#if ENABLE(PERFECT_HASH_SIZE)
    { 15, JSSVGFEDistantLightElementTableValues, 0 };
#else
    { 9, 7, JSSVGFEDistantLightElementTableValues, 0 };
#endif

/* Hash table for constructor */

static const HashTableValue JSSVGFEDistantLightElementConstructorTableValues[1] =
{
    { 0, 0, 0, 0 }
};

static JSC_CONST_HASHTABLE HashTable JSSVGFEDistantLightElementConstructorTable =
#if ENABLE(PERFECT_HASH_SIZE)
    { 0, JSSVGFEDistantLightElementConstructorTableValues, 0 };
#else
    { 1, 0, JSSVGFEDistantLightElementConstructorTableValues, 0 };
#endif

class JSSVGFEDistantLightElementConstructor : public DOMConstructorObject {
public:
    JSSVGFEDistantLightElementConstructor(ExecState* exec, JSDOMGlobalObject* globalObject)
        : DOMConstructorObject(JSSVGFEDistantLightElementConstructor::createStructure(globalObject->objectPrototype()), globalObject)
    {
        putDirect(exec->propertyNames().prototype, JSSVGFEDistantLightElementPrototype::self(exec, globalObject), None);
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

const ClassInfo JSSVGFEDistantLightElementConstructor::s_info = { "SVGFEDistantLightElementConstructor", 0, &JSSVGFEDistantLightElementConstructorTable, 0 };

bool JSSVGFEDistantLightElementConstructor::getOwnPropertySlot(ExecState* exec, const Identifier& propertyName, PropertySlot& slot)
{
    return getStaticValueSlot<JSSVGFEDistantLightElementConstructor, DOMObject>(exec, &JSSVGFEDistantLightElementConstructorTable, this, propertyName, slot);
}

bool JSSVGFEDistantLightElementConstructor::getOwnPropertyDescriptor(ExecState* exec, const Identifier& propertyName, PropertyDescriptor& descriptor)
{
    return getStaticValueDescriptor<JSSVGFEDistantLightElementConstructor, DOMObject>(exec, &JSSVGFEDistantLightElementConstructorTable, this, propertyName, descriptor);
}

/* Hash table for prototype */

static const HashTableValue JSSVGFEDistantLightElementPrototypeTableValues[1] =
{
    { 0, 0, 0, 0 }
};

static JSC_CONST_HASHTABLE HashTable JSSVGFEDistantLightElementPrototypeTable =
#if ENABLE(PERFECT_HASH_SIZE)
    { 0, JSSVGFEDistantLightElementPrototypeTableValues, 0 };
#else
    { 1, 0, JSSVGFEDistantLightElementPrototypeTableValues, 0 };
#endif

const ClassInfo JSSVGFEDistantLightElementPrototype::s_info = { "SVGFEDistantLightElementPrototype", 0, &JSSVGFEDistantLightElementPrototypeTable, 0 };

JSObject* JSSVGFEDistantLightElementPrototype::self(ExecState* exec, JSGlobalObject* globalObject)
{
    return getDOMPrototype<JSSVGFEDistantLightElement>(exec, globalObject);
}

const ClassInfo JSSVGFEDistantLightElement::s_info = { "SVGFEDistantLightElement", &JSSVGElement::s_info, &JSSVGFEDistantLightElementTable, 0 };

JSSVGFEDistantLightElement::JSSVGFEDistantLightElement(NonNullPassRefPtr<Structure> structure, JSDOMGlobalObject* globalObject, PassRefPtr<SVGFEDistantLightElement> impl)
    : JSSVGElement(structure, globalObject, impl)
{
}

JSObject* JSSVGFEDistantLightElement::createPrototype(ExecState* exec, JSGlobalObject* globalObject)
{
    return new (exec) JSSVGFEDistantLightElementPrototype(JSSVGFEDistantLightElementPrototype::createStructure(JSSVGElementPrototype::self(exec, globalObject)));
}

bool JSSVGFEDistantLightElement::getOwnPropertySlot(ExecState* exec, const Identifier& propertyName, PropertySlot& slot)
{
    return getStaticValueSlot<JSSVGFEDistantLightElement, Base>(exec, &JSSVGFEDistantLightElementTable, this, propertyName, slot);
}

bool JSSVGFEDistantLightElement::getOwnPropertyDescriptor(ExecState* exec, const Identifier& propertyName, PropertyDescriptor& descriptor)
{
    return getStaticValueDescriptor<JSSVGFEDistantLightElement, Base>(exec, &JSSVGFEDistantLightElementTable, this, propertyName, descriptor);
}

JSValue jsSVGFEDistantLightElementAzimuth(ExecState* exec, JSValue slotBase, const Identifier&)
{
    JSSVGFEDistantLightElement* castedThis = static_cast<JSSVGFEDistantLightElement*>(asObject(slotBase));
    UNUSED_PARAM(exec);
    SVGFEDistantLightElement* imp = static_cast<SVGFEDistantLightElement*>(castedThis->impl());
    RefPtr<SVGAnimatedNumber> obj = imp->azimuthAnimated();
    JSValue result =  toJS(exec, castedThis->globalObject(), obj.get(), imp);
    return result;
}

JSValue jsSVGFEDistantLightElementElevation(ExecState* exec, JSValue slotBase, const Identifier&)
{
    JSSVGFEDistantLightElement* castedThis = static_cast<JSSVGFEDistantLightElement*>(asObject(slotBase));
    UNUSED_PARAM(exec);
    SVGFEDistantLightElement* imp = static_cast<SVGFEDistantLightElement*>(castedThis->impl());
    RefPtr<SVGAnimatedNumber> obj = imp->elevationAnimated();
    JSValue result =  toJS(exec, castedThis->globalObject(), obj.get(), imp);
    return result;
}

JSValue jsSVGFEDistantLightElementConstructor(ExecState* exec, JSValue slotBase, const Identifier&)
{
    JSSVGFEDistantLightElement* domObject = static_cast<JSSVGFEDistantLightElement*>(asObject(slotBase));
    return JSSVGFEDistantLightElement::getConstructor(exec, domObject->globalObject());
}
JSValue JSSVGFEDistantLightElement::getConstructor(ExecState* exec, JSGlobalObject* globalObject)
{
    return getDOMConstructor<JSSVGFEDistantLightElementConstructor>(exec, static_cast<JSDOMGlobalObject*>(globalObject));
}


}

#endif // ENABLE(SVG) && ENABLE(FILTERS)
