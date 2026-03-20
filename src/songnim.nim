import tables, strutils, streams
import simplepng, nimPNG

type SongInfo = object
  frequencies: OrderedTable[string, seq[int]]
  wordCount: int

proc calcSongSeq(song: string): SongInfo =
  var index = 0
  for word in song.splitWhitespace():
    if word in result.frequencies:
      result.frequencies[word].add(index)
    else:
      result.frequencies[word] = @[index]
    index += 1
  result.wordCount = index

proc makePixels(songInfo: SongInfo): simplepng.Pixels =
  result = initPixels(songInfo.wordCount, songInfo.wordCount)
  result.fill(255, 255, 255, 255)

  for _, locations in songInfo.frequencies:
    for i, occurrence1 in locations:
      result[occurrence1, occurrence1].setColor(0, 0, 0, 255) # diagonal
      for j in (i+1)..<locations.len:
        let occurrence2 = locations[j]
        result[occurrence1, occurrence2].setColor(0, 0, 0, 255)
        result[occurrence2, occurrence1].setColor(0, 0, 0, 255)

proc createPngDataFromString*(song: string): string =
  let pixels = makePixels(calcSongSeq(song))
  var strm = newStringStream()
  defer: strm.close()
  var png = encodePNG(pixels.dataToString(), colorType = LCT_RGBA, 8,
      pixels.width, pixels.height)
  png.writeChunks strm
  return strm.data
