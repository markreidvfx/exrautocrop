/*
 * Copyright (c) 2015 Mark Reid
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include <iostream>
#include <vector>
#include <algorithm>

#include <ImathBox.h>
#include <ImfChannelList.h>
#include <ImfHeader.h>
#include <ImfInputFile.h>
#include <ImfOutputFile.h>
#include <ImfAttribute.h>

using namespace std;
using namespace Imf;
using namespace Imath;

static Box2i min_box(Box2i &a)
{
    Box2i b(a);
    b.max.x = a.min.x;
    b.max.y = a.min.y;
    return b;
}

typedef struct {
    Box2i box;
    bool pixel_seen;
} BoundingBox;

static BoundingBox get_scanline_bounding_box(vector<half> &scanline, Box2i &data_window)
{
    BoundingBox r = {min_box(data_window), false};

    int x = data_window.min.x;

    for(vector<half>::iterator it = scanline.begin(); it != scanline.end(); it++) {
        if (*it != 0.0) {
            if (r.pixel_seen == false) {
                r.box.min.x = x;
                r.box.max.x = x;
                r.pixel_seen = true;

            } else {
                r.box.max.x = x;
            }
        }
        x++;
    }
    return r;
}

static Box2i get_bounding_box(InputFile &file)
{

    Box2i result;
    Box2i data_window = file.header().dataWindow();
    Box2i display_window = file.header().displayWindow();
    int data_width  = data_window.max.x - data_window.min.x + 1;
    int data_height = data_window.max.y - data_window.min.y + 1;

    int channel_count = 0;
    ChannelList channels = file.header().channels();

    for (ChannelList::ConstIterator i = channels.begin(); i != channels.end(); ++i) {
        channel_count++;
    }

    vector < vector < half > > data(channel_count);

    // Setup Frame Buffers
    int index = 0;
    FrameBuffer frame_buffer;

    for (ChannelList::ConstIterator i = channels.begin(); i != channels.end(); ++i) {
        data[index].resize(data_width);
        frame_buffer.insert(i.name(),
                           Slice(HALF,
                                 (char *) &data[index][0] - 2 * data_window.min.x, //??? why
                                 sizeof(data[index][0]) * 1,
                                 sizeof(data[index][0]) * 0, //only single scanline
                                 1, 1,
                                 0.0)
                           );
        index++;
    }

    file.setFrameBuffer(frame_buffer);

    BoundingBox r; // result bounding box
    BoundingBox s_r; // scanline result bounding box

    r.box = min_box(data_window);
    r.pixel_seen = false;

    for (int y = data_window.min.y; y <= data_window.max.y; y++) {

        file.readPixels(y, y);
        index = 0;
        for (ChannelList::ConstIterator i = channels.begin(); i != channels.end(); ++i) {

            s_r = get_scanline_bounding_box(data[index], data_window);

            if (s_r.pixel_seen == true) {

                if (r.pixel_seen == false) {
                    r.pixel_seen = true;
                    r.box.min.x = s_r.box.min.x;
                    r.box.min.y = y;
                    r.box.max.x = s_r.box.max.x;
                    r.box.max.y = y;

                } else {
                    r.box.min.x = min(s_r.box.min.x, r.box.min.x);
                    r.box.max.x = max(s_r.box.max.x, r.box.max.x);
                    r.box.max.y = y;
                }
            }
            index++;
        }
    }
    return r.box;
}

void autocrop(const char in_file_name[], const char out_file_name[])
{
    InputFile in_file(in_file_name);
    Box2i bounding_box = get_bounding_box(in_file);

    Box2i data_window = in_file.header().dataWindow();
    Box2i dw = in_file.header().dataWindow();
    Box2i display_window = in_file.header().displayWindow();

    int width = display_window.max.x - display_window.min.x + 1;
    int height = display_window.max.y - display_window.min.y + 1;

    int data_width  = data_window.max.x - data_window.min.x + 1;
    int data_height = data_window.max.y - data_window.min.y + 1;

    int bb_width = bounding_box.max.x - bounding_box.min.x + 1;
    int bb_height = bounding_box.max.y - bounding_box.min.y + 1;

    cerr << "Display Window : " << display_window.min << " " << display_window.max << " " << width << "x" << height << endl;
    cerr << "Data Window    : " << data_window.min << " " << data_window.max << " " << data_width << "x" << data_height << endl;
    cerr << "Bounding Box   : " << bounding_box.min << " " << bounding_box.max << " " << bb_width << "x" << bb_height << endl;

    // Create a new header using Bounding Box
    Header h(display_window, bounding_box);

    // Copy InputFile Header Attributes to OutputFile Header
    for (Header::ConstIterator i = in_file.header().begin(); i != in_file.header().end(); ++i) {
        std::string name = i.name();

        if (name == "channels"   ||
            name == "dataWindow" ||
            name == "displayWindow" ||
            name == "lineOrder" ||
            name == "tiles") {
            continue;
        }

        Attribute *tmp = i.attribute().copy();
        h.insert(i.name(), *tmp);
        delete tmp;
    }

    // Copy InputFile Channels to OutputFile Header
    int channel_count = 0;
    ChannelList channels = in_file.header().channels();

    for (ChannelList::ConstIterator i = channels.begin(); i != channels.end(); ++i) {
        h.channels().insert(i.name(), Channel(HALF)); // Convert Channels to half float
        channel_count++;
    }

    h.compression() = ZIPS_COMPRESSION; //optimal compression for compositing

    OutputFile out_file(out_file_name, h);
    vector < vector < half > > data(channel_count);

    // Setup Frame Buffer
    int index = 0;
    FrameBuffer frame_buffer;
    for (ChannelList::ConstIterator i = channels.begin(); i != channels.end(); ++i) {
        data[index].resize(data_width);

        frame_buffer.insert(i.name(),
                            Slice(HALF,
                                 (char *) &data[index][0] - 2 * data_window.min.x,
                                 sizeof(data[index][0]) * 1,
                                 sizeof(data[index][0]) * 0, //only single scanline
                                 1, 1,
                                 0.0)
                           );
        index++;
    }

    in_file.setFrameBuffer(frame_buffer);
    out_file.setFrameBuffer(frame_buffer);

    // Read and Write Scanlines
    for (int y=bounding_box.min.y; y <= bounding_box.max.y; y++) {

        in_file.readPixels(y);
        out_file.writePixels(1);
    }

}

void usage_message(const char argv0[])
{
    cerr << "usage: " << argv0 << " [options] source_file target_file" << endl;
    cerr << "       -h Display this usage information." << endl;
}

int main(int argc, char **argv)
{
    int i;

    if (argc < 3) {
        usage_message(argv[0]);
        return 1;
    }

    for(i = 1; i < argc; ++i) {
        if(!strcmp (argv[i], "-h")) {
            usage_message(argv[0]);
            return 1;
        }
    }

    try
    {
        autocrop(argv[1], argv[2]);
        return 0;
    }
    catch (const std::exception &e) {
        cerr << e.what() << std::endl;
        return 1;
    }
    return 0;
}