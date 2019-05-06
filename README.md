Y4MCreator

Goal
The goal of this utility is to decode an input file or url, usually mp4's, into an uncompressed Y4M file. We leverage ffmpeg's libavformat and libavcodec libraries to reach this goal. Libavformat provides a vast amount of demuxers and also the muxer we need, y4m. libavcodec also contains an h264 decoder that we will definitely need. 

Approach to output frames in coded order
A direct approach would involve straight calls onto the decoder, modifying the output frame routines so we would skip frame reordering. This would require modification of the h264 decoder inside libavcodec and would also require to lock the main app to a custom version of ffmpeg.

Another approach, which is the chosen one, consists of using the 'coded_picture_number' field of the AVFrame structure coming from the decoder. 
