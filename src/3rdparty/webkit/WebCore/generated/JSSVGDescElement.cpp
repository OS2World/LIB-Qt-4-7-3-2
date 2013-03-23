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

#include "JSSVGDescElement.h"

#include "CSSMutableStyleDeclaration.h"
#include "CSSStyleDeclaration.h"
#include "CSSValue.h"
#include "JSCSSStyleDeclaration.h"
#include "JSCSSValue.h"
#include "JSSVGAnimatedString.h"
#include "KURL.h"
#include "SVGDescElement.h"
#include <runtime/Error.h>
#include <runtime/JSString.h>
#include <wtf/GetPtr.h>

using namespace JSC;

namespace WebCore {

ASSERT_CLASS_FITS_IN_CELL(JSSVGDescElement);

/* Hash table */

static const HashTableValue JSSVGDescElementTableValues[6] =
{
    { "xmllang", DontDelete, (intptr_t)static_cast<PropertySlot::GetValueFunc>(jsSVGDescElementXmllang), (intptr_t)setJSSVGDescElementXmllang },
    { "xmlspace", DontDelete, (intptr_t)static_cast<PropertySlot::GetValueFunc>(jsSVGDescElementXmlspace), (intptr_t)setJSSVGDescElementXmlspace },
    { "className", DontDelete|ReadOnly, (intptr_t)static_cast<PropertySlot::GetValueFunc>(jsSVGDescElementClassName), (intptr_t)0 },
    { "style", DontDelete|ReadOnly, (intptr_t)static_cast<PropertySlot::GetValueFunc>(jsSVGDescElementStyle), (intptr_t)0 },
    { "constructor", DontEnum|ReadOnly, (intptr_t)static_cast<PropertySlot::GetValueFunc>(jsSVGDescElementConstructor), (intptr_t)0 },
    { 0, 0, 0, 0 }
};

static JSC_CONST_HASHTABLE HashTable JSSVGDescElementTable =
#if ENABLE(PERFECT_HASH_SIZE)
    { 15, JSSVGDescElementTableValues, 0 };
#else
    { 16, 15, JSSVGDescElementTableValues, 0 };
#endif

/* Hash table for constructor */

static const HashTableValue JSSVGDescElementConstructorTableValues[1] =
{
    { 0, 0, 0, 0 }
};

static JSC_CONST_HASHTABLE HashTable JSSVGDescElementConstructorTable =
#if ENABLE(PERFECT_HASH_SIZE)
    { 0, JSSVGDescElementConstructorTableValues, 0 };
#else
    { 1, 0, JSSVGDescElementConstructorTableValues, 0 };
#endif

class JSSVGDescElementConstructor : public DOMConstructorObject {
public:
    JSSVGDescElementConstructor(ExecState* exec, JSDOMGlobalObject* globalObject)
        : DOMConstructorObject(JSSVGDescElementConstructor::createStructure(globalObject->objectPrototype()), globalObject)
    {
        putDirect(exec->propertyNames().prototype, JSSVGDescElementPrototype::self(exec, globalObject), None);
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

const ClassInfo JSSVGDescElementConstructor::s_info = { "SVGDescElementConstructor", 0, &JSSVGDescElementConstructorTable, 0 };

bool JSSVGDescElementConstructor::getOwnPropertySlot(ExecState* exec, const Identifier& propertyName, PropertySlot& slot)
{
    return getStaticValueSlot<JSSVGDescElementConstructor, DOMObject>(exec, &JSSVGDescElementConstructorTable, this, propertyName, slot);
}

bool JSSVGDescElementConstructor::getOwnPropertyDescriptor(ExecState* exec, const Identifier& propertyName, PropertyDescriptor& descriptor)
{
    return getStaticValueDescriptor<JSSVGDescElementConstructor, DOMObject>(exec, &JSSVGDescElementConstructorTable, this, propertyName, descriptor);
}

/* Hash table for prototype */

static const HashTableValue JSSVGDescElementPrototypeTableValues[2] =
{
    { "getPresentationAttribute", DontDelete|Function, (intptr_t)static_cast<NativeFunction>(jsSVGDescElementPrototypeFunctionGetPresentationAttribute), (intptr_t)1 },
    { 0, 0, 0, 0 }
};

static JSC_CONST_HASHTABLE HashTable JSSVGDescElementPrototypeTable =
#if ENABLE(PERFECT_HASH_SIZE)
    { 0, JSSVGDescElementPrototypeTableValues, 0 };
#else
    { 2, 1, JSSVGDescElementPrototypeTableValues, 0 };
#endif

const ClassInfo JSSVGDescElementPrototype::s_info = { "SVGDescElementPrototype", 0, &JSSVGDescElementPrototypeTable, 0 };

JSObject* JSSVGDescElementPrototype::self(ExecState* exec, JSGlobalObject* globalObject)
{
    return getDOMPrototype<JSSVGDescElement>(exec, globalObject);
}

bool JSSVGDescElementPrototype::getOwnPropertySlot(ExecState* exec, const Identifier& propertyName, PropertySlot& slot)
{
    return getStaticFunctionSlot<JSObject>(exec, &JSSVGDescElementPrototypeTable, this, propertyName, slot);
}

bool JSSVGDescElementPrototype::getOwnPropertyDescriptor(ExecState* exec, const Identifier& propertyName, PropertyDescriptor& descriptor)
{
    return getStaticFunctionDescriptor<JSObject>(exec, &JSSVGDescElementPrototypeTable, this, propertyName, descriptor);
}

const ClassInfo JSSVGDescElement::s_info = { "SVGDescElement", &JSSVGElement::s_info, &JSSVGDescElementTable, 0 };

JSSVGDescElement::JSSVGDescElement(NonNullPassRefPtr<Structure> structure, JSDOMGlobalObject* globalObject, PassRefPtr<SVGDescElement> impl)
    : JSSVGElement(structure, globalObject, impl)
{
}

JSObject* JSSVGDescElement::createPrototype(ExecState* exec, JSGlobalObject* globalObject)
{
    return new (exec) JSSVGDescElementPrototype(JSSVGDescElementPrototype::createStructure(JSSVGElementPrototype::self(exec, globalObject)));
}

bool JSSVGDescElement::getOwnPropertySlot(ExecState* exec, const Identifier& propertyName, PropertySlot& slot)
{
    return getStaticValueSlot<JSSVGDescElement, Base>(exec, &JSSVGDescElementTable, this, propertyName, slot);
}

bool JSSVGDescElement::getOwnPropertyDescriptor(ExecState* exec, const Identifier& propertyName, PropertyDescriptor& descriptor)
{
    return getStaticValueDescriptor<JSSVGDescElement, Base>(exec, &JSSVGDescElementTable, this, propertyName, descriptor);
}

JSValue jsSVGDescElementXmllang(ExecState* exec, JSValue slotBase, const Identifier&)
{
    JSSVGDescElement* castedThis = static_cast<JSSVGDescElement*>(asObject(slotBase));
    UNUSED_PARAM(exec);
    SVGDescElement* imp = static_cast<SVGDescElement*>(castedThis->impl());
    JSValue result = jsString(exec, imp->xmllang());
    return result;
}

JSValue jsSVGDescElementXmlspace(ExecState* exec, JSValue slotBase, const Identifier&)
{
    JSSVGDescElement* castedThis = static_cast<JSSVGDescElement*>(asObject(slotBase));
    UNUSED_PARAM(exec);
    SVGDescElement* imp = static_cast<SVGDescElement*>(castedThis->impl());
    JSValue result = jsString(exec, imp->xmlspace());
    return result;
}

JSValue jsSVGDescElementClassName(ExecState* exec, JSValue slotBase, const Identifier&)
{
    JSSVGDescElement* castedThis = static_cast<JSSVGDescElement*>(asObject(slotBase));
    UNUSED_PARAM(exec);
    SVGDescElement* imp = static_cast<SVGDescElement*>(castedThis->impl());
    RefPtr<SVGAnimatedString> obj = imp->classNameAnimated();
    JSValue result =  toJS(exec, castedThis->globalObject(), obj.get(), imp);
    return result;
}

JSValue jsSVGDescElementStyle(ExecState* exec, JSValue slotBase, const Identifier&)
{
    JSSVGDescElement* castedThis = static_cast<JSSVGDescElement*>(asObject(slotBase));
    UNUSED_PARAM(exec);
    SVGDescElement* imp = static_cast<SVGDescElement*>(castedThis->impl());
    JSValue result = toJS(exec, castedThis->globalObject(), WTF::getPtr(imp->style()));
    return result;
}

JSValue jsSVGDescElementConstructor(ExecState* exec, JSValue slotBase, const Identifier&)
{
    JSSVGDescElement* domObject = static_cast<JSSVGDescElement*>(asObject(slotBase));
    return JSSVGDescElement::getConstructor(exec, domObject->globalObject());
}
void JSSVGDescElement::put(ExecState* exec, const Identifier& propertyName, JSValue value, PutPropertySlot& slot)
{
    lookupPut<JSSVGDescElement, Base>(exec, propertyName, value, &JSSVGDescElementTable, this, slot);
}

void setJSSVGDescElementXmllang(ExecState* exec, JSObject* thisObject, JSValue value)
{
    JSSVGDescElement* castedThisObj = static_cast<JSSVGDescElement*>(thisObject);
    SVGDescElement* imp = static_cast<SVGDescElement*>(castedThisObj->impl());
    imp->setXmllang(value.toString(exec));
}

void setJSSVGDescElementXmlspace(ExecState* exec, JSObject* thisObject, JSValue value)
{
    JSSVGDescElement* castedThisObj = static_cast<JSSVGDescElement*>(thisObject);
    SVGDescElement* imp = static_cast<SVGDescElement*>(castedThisObj->impl());
    imp->setXmlspace(value.toString(exec));
}

JSValue JSSVGDescElement::getConstructor(ExecState* exec, JSGlobalObject* globalObject)
{
    return getDOMConstructor<JSSVGDescElementConstructor>(exec, static_cast<JSDOMGlobalObject*>(globalObject));
}

JSValue JSC_HOST_CALL jsSVGDescElementPrototypeFunctionGetPresentationAttribute(ExecState* exec, JSObject*, JSValue thisValue, const ArgList& args)
{
    UNUSED_PARAM(args);
    if (!thisValue.inherits(&JSSVGDescElement::s_info))
        return throwError(exec, TypeError);
    JSSVGDescElement* castedThisObj = static_cast<JSSVGDescElement*>(asObject(thisValue));
    SVGDescElement* imp = static_cast<SVGDescElement*>(castedThisObj->impl());
    const UString& name = args.at(0).toString(exec);


    JSC::JSValue result = toJS(exec, castedThisObj->globalObject(), WTF::getPtr(imp->getPresentationAttribute(name)));
    return result;
}


}

#endif // ENABLE(SVG)