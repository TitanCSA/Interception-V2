#include "library/interception.h"
#include "WireTypes.h"
#include <map>
struct ProductSession{

InterceptionContext context=nullptr;
~ProductSession(){if(context)interception_destroy_context(context);}
InterceptionContext Get(){if(!context){context=interception_create_context();if(!context)throw SdkError(GetLastError()?GetLastError():ERROR_OPEN_FAILED,"Cannot create interception context");}return context;}
static int Any(InterceptionDevice device){return !interception_is_invalid(device);}
int Device(const Json& a){Require(a.contains("device"),"Device is required");auto device=static_cast<int>(Number(a,"device",0,20));Require(!interception_is_invalid(device),"Invalid interception device");return device;}

Json Invoke(const std::string& op,const Json& a){

if(op=="check"){auto device=Device(a);return {{"valid",true},{"keyboard",bool(interception_is_keyboard(device))},{"mouse",bool(interception_is_mouse(device))}};}
if(op=="initialize"){Get();return Json::object();}
if(op=="close"){if(context){interception_destroy_context(context);context=nullptr;}return Json::object();}
if(op=="get_precedence")return {{"precedence",interception_get_precedence(Get(),Device(a))}};
if(op=="set_precedence"){interception_set_precedence(Get(),Device(a),FromWire<int>(a.at("precedence")));return Json::object();}
if(op=="get_filter")return {{"filter",interception_get_filter(Get(),Device(a))}};
if(op=="set_filter"){auto kind=a.at("kind").get<std::string>();InterceptionPredicate predicate=nullptr;if(kind=="keyboard")predicate=interception_is_keyboard;else if(kind=="mouse")predicate=interception_is_mouse;else if(kind=="all")predicate=Any;else throw SdkError(87,"Filter kind must be keyboard, mouse or all");interception_set_filter(Get(),predicate,static_cast<InterceptionFilter>(Number(a,"filter",0,65535)));return Json::object();}
if(op=="wait")return {{"device",interception_wait_with_timeout(Get(),Number(a,"timeout",0,100))}};
if(op=="receive"){auto device=Device(a);auto limit=Number(a,"limit",1,1024);Require(limit>0,"Limit must be positive");std::vector<InterceptionMouseStroke> storage(limit);int count=interception_receive(Get(),device,reinterpret_cast<InterceptionStroke*>(storage.data()),limit);Require(count>=0 && static_cast<unsigned>(count)<=limit,"Invalid stroke count");Json strokes=Json::array();for(int i=0;i<count;++i){if(interception_is_keyboard(device)){InterceptionKeyStroke key{};std::memcpy(&key,reinterpret_cast<const char*>(storage.data())+i*sizeof(InterceptionKeyStroke),sizeof(key));strokes.push_back(EncodeWire(key));}else strokes.push_back(EncodeWire(storage[i]));}return {{"device",device},{"strokes",strokes}};}
if(op=="send"){auto device=Device(a);auto strokes=a.at("strokes");Require(strokes.is_array() && !strokes.empty() && strokes.size()<=1024,"Expected 1..1024 strokes");int accepted=0;if(interception_is_keyboard(device)){std::vector<InterceptionKeyStroke> keys;for(const auto& value:strokes)keys.push_back(FromWire<InterceptionKeyStroke>(value));accepted=interception_send(Get(),device,reinterpret_cast<const InterceptionStroke*>(keys.data()),static_cast<unsigned>(keys.size()));}else{std::vector<InterceptionMouseStroke> mouse;for(const auto& value:strokes)mouse.push_back(FromWire<InterceptionMouseStroke>(value));accepted=interception_send(Get(),device,reinterpret_cast<const InterceptionStroke*>(mouse.data()),static_cast<unsigned>(mouse.size()));}Require(accepted>=0 && static_cast<std::size_t>(accepted)<=strokes.size(),"Invalid native accepted count");return {{"accepted",accepted}};}
if(op=="hardware_id"){wchar_t buffer[4096]{};auto bytes=interception_get_hardware_id(Get(),Device(a),buffer,sizeof(buffer));auto count=(std::min)(static_cast<std::size_t>(bytes/sizeof(wchar_t)),std::size(buffer));auto end=std::find(buffer,buffer+count,L'\0');return {{"hardware_id",Utf8(std::wstring(buffer,end))}};}

throw SdkError(50,"Unknown operation");}
};
static Runtime<ProductSession> runtime;
extern "C" __declspec(dllexport) unsigned interception_v2_json_open(std::uint64_t* id)noexcept{return runtime.Open(id);}
extern "C" __declspec(dllexport) unsigned interception_v2_json_close(std::uint64_t id)noexcept{return runtime.Close(id);}
extern "C" __declspec(dllexport) char* interception_v2_json_call(std::uint64_t id,const char* request,std::uint32_t length)noexcept{return runtime.Call(id,request,length);}
extern "C" __declspec(dllexport) void interception_v2_json_free(char* response)noexcept{delete[] response;}
