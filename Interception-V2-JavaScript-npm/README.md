# Interception-V2 Node.js / TypeScript SDK

`npm install interception-v2`

Windows x64 Node20+, prebuilt Node-API8 addon and product DLL.

```js
const {Client}=require("interception-v2");
const client=new Client();
try {console.log(client.call("status"));}finally{client.close();}
```

Call is synchronous; Calls use the native operation deadlines. Run client in a worker for responsive UI/event loops. NativeError exposes code. [Operations and schema](docs/API.md). Matching driver and original product licensing required separately.
