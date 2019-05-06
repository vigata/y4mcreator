# Y4MCreator

y4mcreator is a tool that takes a video file and outputs an uncompressed YUV version in .y4m format for the file.It can output the video in presentation order or in decoded order. 


# Architecture
We leverage ffmpeg's libavformat and libavcodec libraries to create this tool. Libavformat provides a vast amount of demuxers and also the muxer we need, y4m. libavcodec also contains an h264 decoder. 

## Approach to output frames in coded order
A direct approach would involve straight calls onto the decoder, modifying the decoder output frame routines so we would skip frame reordering. This would require modification of the h264 decoder inside libavcodec and would also require to lock the main app to a custom version of ffmpeg.

Another approach, which is the chosen one, consists of using the 'coded_picture_number' field of the AVFrame structure coming from the decoder. Because by default the decoder outputs the frames in display order, we need to engineer a mechanism to reorder the frames in coded order. The requirements of this system are:

* efficient algorithm for frame reordering
* minimal buffering required
* underlying GOP structure independence

A min heap or priority queue [https://en.wikipedia.org/wiki/Heap_(data_structure)] allows to comply with all requirements. If we don't know the underlying structure of the reference frames of the encoder, we are essentially reordering the frames through the use of a heap. This is an optimal structure for ordering. In our case it has a cost of O(log n) per frame of video, where ni is the naximum number of reference frames in the video.

A full and detailed implementation of a min-heap for AVFrames is presented in the code. Further comments are laid out in the code.


## Frame demuxing, decoding, and y4m muxing

For the input format demuxing (usually mp4), video decoding and muxing into y4m, we do full use of libavformat and libavcodec facilities. The code is fairly explanatory and contains further comments but the flow of data is as follows:
1. Read data from the demuxer
2. pass data to decoder
3. If frame decoded push into the heap 
4. pop from the top of the heap 
5. write to output muxer (y4m)








## Usage and testing

`y4mcreator [file:input.mp4 | url] [out.y4m] [d]`

The tool accepts either local files or a url. Output file must be in .y4m extension. 

In order to test the correct behavior of the program the optimal 'd' third parameter can be added. This will print out the coded_picture_number of the frames being written to disk. If algorithm is correct, this numbers should be consecutive. 

By default tool shows frames in coded order. There's an internal flag in main(), `codec_order` which if set to zero produces the standard presentation order. 



## Building
### Requirements

* CMake
* C Compiler
* ffmpeg 4+ development libraries
* pkg-config

There's a provided CMake configuration file to create a platform specific Makefile. Run `cmake CMakeList.txt` to produce the makefile. Then do `make` 


## Known issues
* Tool assumes first `coded_picture_number` is always 0
* by default coded_order=1 . A recompile is necessary to get presentation order.



