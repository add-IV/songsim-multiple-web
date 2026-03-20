import tables, strutils, os, uri, base64, strformat, streams
import simplepng, nimPNG
import jester

proc calcSongSeq(song: string): (OrderedTable[string, seq[int]], int)  =
  var
    count = 0
  for word in song.splitWhitespace():
    if word in result[0]:
      result[0][word].add(count)
    else:
      result[0][word] = @[count]
    count += 1
  result[1] = count
  
proc makePng(songTable: OrderedTable[string, seq[int]], count: int): string=
  var p = initPixels(count+1, count+1)
  p.fill(255, 255, 255, 255)
  for _, where in songTable:
    for num1 in where:
      for num2 in where:
        p[num1, num2].setColor(0, 0, 0, 255)
  var strm = newStringStream()
  var png = encodePNG(p.dataToString(), colorType=LCT_RGBA, 8, p.width, p.height)
  png.writeChunks strm
  return strm.data

router myrouter:
  get "/":
    try:
      let text = decodeUrl(request.query)
      echo text
      let songTable = calcSongSeq(text)
      let data = makePng(songTable[0], songTable[1])
      echo data
      let encoded = encode(data)
      echo encoded
      resp fmt"<img src='data:image/png;base64,{encoded}'>"
    except:
      resp "error"

proc main() =
  var port: Port
  if paramCount() == 2:
    port = paramStr(1).parseInt().Port
  else:
    port = Port(8080)
  let settings = newSettings(port=port)
  var jester = initJester(myrouter, settings=settings)
  jester.serve()

when isMainModule:
  main()