/*
 * Created by James Chen on 3/11/13.
 * Copyright (c) 2013-2017 Chukong Technologies Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "scripting/js-bindings/manual/extension/jsb_cocos2dx_extension_manual.h"
#include "extensions/cocos-ext.h"
#include "scripting/js-bindings/manual/ScriptingCore.h"
#include "scripting/js-bindings/manual/cocos2d_specifics.hpp"
#include "scripting/js-bindings/auto/jsb_cocos2dx_auto.hpp"
#include <thread>
#include <chrono>

#include "base/CCDirector.h"
#include "base/CCScheduler.h"
#include "renderer/CCTextureCache.h"
#include "renderer/CCTextureCube.h"

USING_NS_CC;
USING_NS_CC_EXT;


__JSDownloaderDelegator::__JSDownloaderDelegator(JSContext *cx, JS::HandleObject obj, const std::string &url, JS::HandleObject callback)
: _cx(cx)
, _url(url)
{
    _obj = obj;
    _jsCallback = callback;

    JS::RootedValue target(cx, OBJECT_TO_JSVAL(obj));
    if (!target.isNullOrUndefined())
    {
        js_add_object_root(target);
    }
    target.set(OBJECT_TO_JSVAL(callback));
    if (!target.isNullOrUndefined())
    {
        js_add_object_root(target);
    }
}

__JSDownloaderDelegator::~__JSDownloaderDelegator()
{
    JS::RootedValue target(_cx, OBJECT_TO_JSVAL(_obj));
    if (!target.isNullOrUndefined())
    {
        js_remove_object_root(target);
    }
    target.set(OBJECT_TO_JSVAL(_jsCallback));
    if (!target.isNullOrUndefined())
    {
        js_remove_object_root(target);
    }

    _downloader->onTaskError = (nullptr);
    _downloader->onDataTaskSuccess = (nullptr);
}

__JSDownloaderDelegator *__JSDownloaderDelegator::create(JSContext *cx, JS::HandleObject obj, const std::string &url, JS::HandleObject callback)
{
    __JSDownloaderDelegator *delegate = new (std::nothrow) __JSDownloaderDelegator(cx, obj, url, callback);
    delegate->autorelease();
    return delegate;
}

void __JSDownloaderDelegator::startDownload()
{
    if (auto texture = Director::getInstance()->getTextureCache()->getTextureForKey(_url))
    {
        onSuccess(texture);
    }
    else
    {
        _downloader = std::make_shared<cocos2d::network::Downloader>();
//        _downloader->setConnectionTimeout(8);
        _downloader->onTaskError = [this](const cocos2d::network::DownloadTask& task,
                                          int errorCode,
                                          int errorCodeInternal,
                                          const std::string& errorStr)
        {
            this->onError();
        };

        _downloader->onDataTaskSuccess = [this](const cocos2d::network::DownloadTask& task,
                                                std::vector<unsigned char>& data)
        {
            Image* img = new (std::nothrow) Image();
            Texture2D *tex = nullptr;
            do
            {
                if (false == img->initWithImageData(data.data(), data.size()))
                {
                    break;
                }
                tex = Director::getInstance()->getTextureCache()->addImage(img, _url);
            } while (0);

            CC_SAFE_RELEASE(img);

            if (tex)
            {
                this->onSuccess(tex);
            }
            else
            {
                this->onError();
            }
        };

        _downloader->createDownloadDataTask(_url);
    }
}

void __JSDownloaderDelegator::download()
{
    retain();
    startDownload();
}

void __JSDownloaderDelegator::downloadAsync()
{
    retain();
    auto t = std::thread(&__JSDownloaderDelegator::startDownload, this);
    t.detach();
}

void __JSDownloaderDelegator::onError()
{
    Director::getInstance()->getScheduler()->performFunctionInCocosThread([this]
    {
        JS::RootedValue callback(_cx, OBJECT_TO_JSVAL(_jsCallback));
        if (!callback.isNull()) {
            JS::RootedObject global(_cx, ScriptingCore::getInstance()->getGlobalObject());
            JSAutoCompartment ac(_cx, global);

            jsval succeed = BOOLEAN_TO_JSVAL(false);
            JS::RootedValue retval(_cx);
            JS_CallFunctionValue(_cx, global, callback, JS::HandleValueArray::fromMarkedLocation(1, &succeed), &retval);
        }
        release();
    });
}

void __JSDownloaderDelegator::onSuccess(Texture2D *tex)
{
    CCASSERT(tex, "__JSDownloaderDelegator::onSuccess must make sure tex not null!");
    //Director::getInstance()->getScheduler()->performFunctionInCocosThread([this, tex]
    {
        JS::RootedObject global(_cx, ScriptingCore::getInstance()->getGlobalObject());
        JSAutoCompartment ac(_cx, global);

        jsval valArr[2];
        if (tex)
        {
            valArr[0] = BOOLEAN_TO_JSVAL(true);
            JS::RootedObject jsobj(_cx, js_get_or_create_jsobject<Texture2D>(_cx, tex));
            valArr[1] = OBJECT_TO_JSVAL(jsobj);
        }
        else
        {
            valArr[0] = BOOLEAN_TO_JSVAL(false);
            valArr[1] = JSVAL_NULL;
        }

        JS::RootedValue callback(_cx, OBJECT_TO_JSVAL(_jsCallback));
        if (!callback.isNull())
        {
            JS::RootedValue retval(_cx);
            JS_CallFunctionValue(_cx, global, callback, JS::HandleValueArray::fromMarkedLocation(2, valArr), &retval);
        }
        release();
    }//);
}

// jsb.loadRemoteImg(url, function(succeed, result) {})
bool js_load_remote_image(JSContext *cx, uint32_t argc, jsval *vp)
{
    JS::CallArgs args = JS::CallArgsFromVp(argc, vp);
    JS::RootedObject obj(cx, args.thisv().toObjectOrNull());
    if (argc == 2)
    {
        std::string url;
        bool ok = jsval_to_std_string(cx, args.get(0), &url);
        JSB_PRECONDITION2(ok, cx, false, "js_load_remote_image : Error processing arguments");
        JS::RootedObject callback(cx, args.get(1).toObjectOrNull());

        __JSDownloaderDelegator *delegate = __JSDownloaderDelegator::create(cx, obj, url, callback);
        delegate->downloadAsync();

        args.rval().setUndefined();
        return true;
    }

    JS_ReportError(cx, "js_load_remote_image : wrong number of arguments");
    return false;
}

using namespace std::chrono;

bool js_performance_now(JSContext *cx, uint32_t argc, jsval *vp)
{
	JS::CallArgs args = JS::CallArgsFromVp(argc, vp);
	auto now = steady_clock::now();
	auto micro = duration_cast<microseconds>(now - ScriptingCore::getInstance()->getEngineStartTime()).count();
	args.rval().set(DOUBLE_TO_JSVAL((double)micro * 0.001));
	return true;
}


void register_all_cocos2dx_extension_manual(JSContext* cx, JS::HandleObject global)
{
    JS::RootedObject ccObj(cx);
    JS::RootedObject jsbObj(cx);
    get_or_create_js_obj(cx, global, "cc", &ccObj);
    get_or_create_js_obj(cx, global, "jsb", &jsbObj);

    JS_DefineFunction(cx, jsbObj, "loadRemoteImg", js_load_remote_image, 2, JSPROP_READONLY | JSPROP_PERMANENT);

    JS::RootedObject performance(cx);
    get_or_create_js_obj(cx, global, "performance", &performance);
    JS_DefineFunction(cx, performance, "now", js_performance_now, 0, JSPROP_ENUMERATE | JSPROP_PERMANENT);
}
