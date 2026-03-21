import os, uri, base64, strformat, strutils
import jester
import songnim
import ffi

const imgStyle = "style='width:80vmin;height:80vmin;image-rendering:pixelated'"

router myrouter:
  get "/nim":
    try:
      let text = decodeUrl(request.query)
      let data = createPngDataFromString(text)
      let encoded = encode(data)
      resp fmt"<img {imgStyle} src='data:image/png;base64,{encoded}'>"
    except:
      resp "error"
  get "/rust":
    try:
      let text = decodeUrl(request.query)
      var dataLen: csize_t
      let pngBuf = songsimRust(text.cstring, addr dataLen)
      if pngBuf == nil:
        resp "error"
      else:
        defer: songsimRustFree(pngBuf, dataLen)
        let encoded = encode(pngBuf.toOpenArray(0, dataLen.int - 1))
        resp fmt"<img {imgStyle} src='data:image/png;base64,{encoded}'>"
    except:
      resp "error"
  get "/c":
    try:
      let text = decodeUrl(request.query)
      var dataLen: csize_t
      let pngBuf = songsimC(text.cstring, addr dataLen)
      if pngBuf == nil:
        resp "error"
      else:
        defer: songsimCFree(pngBuf, dataLen)
        let encoded = encode(pngBuf.toOpenArray(0, dataLen.int - 1))
        resp fmt"<img {imgStyle} src='data:image/png;base64,{encoded}'>"
    except:
      resp "error"

proc main() =
  var port: Port
  if paramCount() == 1:
    port = paramStr(1).parseInt().Port
  else:
    port = Port(8080)
  let settings = newSettings(port = port)
  var jester = initJester(myrouter, settings = settings)
  jester.serve()

when isMainModule:
  main()
