#include <nvdsgstutils.h>
#include <cuda_runtime_api.h>
#include <gstnvdsmeta.h>
#include <glib.h>
#include <iostream>
#include <string>
#include <fstream>
#include <iomanip>
#include <chrono>
#include <sstream>
#include <ctime>



#include "perf.h"

#define VIDEO_PATH "/home/lin/DeepStream-Yolo-Pose/bodypose.mp4"
#define CONFIG_FILE_PATH "/home/lin/DeepStream-Yolo-Pose/config_infer_primary_yoloV8_pose.txt"

#define PERF_MEASUREMENT_INTERVAL_SEC 2 // 性能测量间隔时间
#define SET_WIDTH 1920
#define SET_HEIGHT 1080
#define STREAMMUX_TIMEOUT 16000
#define CAPTURE_MODE 1    // 1: 文件读取视频；0: 摄像头读取视频
#define VIDEO_SAVE_MODE 1 // 0: 不保存视频,实时演示；1: 保存视频，不实时演示

const gint skeleton[][2] = {{16, 14}, {14, 12}, {17, 15}, {15, 13}, {12, 13}, {6, 12}, {7, 13}, {6, 7}, {6, 8}, {7, 9}, {8, 10}, {9, 11}, {2, 3}, {1, 2}, {1, 3}, {2, 4}, {3, 5}, {4, 6}, {5, 7}};

static gboolean
bus_call(GstBus *bus, GstMessage *msg, gpointer user_data)
{
    GMainLoop *loop = (GMainLoop *)user_data;
    switch (GST_MESSAGE_TYPE(msg))
    {
    case GST_MESSAGE_EOS:
    {
        g_print("Received EOS event...\n");
        g_print("Waiting for pipeline to finish processing...\n");

        // 确保所有数据都被处理完
        GstState state;
        GstState pending;
        gst_element_get_state(GST_ELEMENT(GST_MESSAGE_SRC(msg)), &state, &pending, GST_CLOCK_TIME_NONE);

        g_print("Pipeline processing complete. Exiting...\n");
        g_main_loop_quit(loop);
        break;
    }
    case GST_MESSAGE_WARNING:
    {
        gchar *debug;
        GError *error;
        gst_message_parse_warning(msg, &error, &debug);
        g_printerr("WARNING: %s: %s\n", GST_OBJECT_NAME(msg->src), error->message);
        g_free(debug);
        g_error_free(error);
        break;
    }
    case GST_MESSAGE_ERROR:
    {
        gchar *debug;
        GError *error;
        gst_message_parse_error(msg, &error, &debug);
        g_printerr("ERROR: %s: %s\n", GST_OBJECT_NAME(msg->src), error->message);
        g_free(debug);
        g_error_free(error);
        g_main_loop_quit(loop);
        break;
    }
    default:
        break;
    }
    return TRUE;
}

// 用于按键退出
static gboolean key_event(GIOChannel *source, GIOCondition condition, gpointer data)
{
    GMainLoop *loop = (GMainLoop *)data;
    g_main_loop_quit(loop);
    return TRUE;
}

// 时间命名函数
std::string getCurrentTimeFilename(const char *file_format)
{
    // 获取当前时间点
    auto now = std::chrono::system_clock::now();
    std::time_t now_time = std::chrono::system_clock::to_time_t(now);

    // 获取文件格式名
    std::string file_format_name = file_format;

    // 格式化时间
    std::tm tm_info;
    localtime_r(&now_time, &tm_info); // Linux/macOS

    std::ostringstream oss;
    oss << std::put_time(&tm_info, "%Y-%m-%d_%H-%M-%S");

    return "pose" + oss.str() + file_format_name; // 生成文件名
}

// pad-added 回调函数实现
static void pad_added_handler(GstElement *src, GstPad *new_pad, gpointer data)
{
    GstElement *streammux = (GstElement *)data;
    GstPad *sink_pad = gst_element_get_request_pad(streammux, "sink_0");
    GstPadLinkReturn ret;
    GstCaps *new_pad_caps = NULL;
    GstStructure *new_pad_struct = NULL;
    const gchar *new_pad_type = NULL;

    g_print("Received new pad '%s' from '%s':\n", GST_PAD_NAME(new_pad), GST_ELEMENT_NAME(src));

    // 检查新pad的类型
    new_pad_caps = gst_pad_get_current_caps(new_pad);
    new_pad_struct = gst_caps_get_structure(new_pad_caps, 0);
    new_pad_type = gst_structure_get_name(new_pad_struct);

    g_print("  Pad type: %s\n", new_pad_type);

    // 如果是视频数据，链接到streammux
    if (g_str_has_prefix(new_pad_type, "video/x-raw"))
    {
        // 链接pad
        ret = gst_pad_link(new_pad, sink_pad);
        if (GST_PAD_LINK_FAILED(ret))
        {
            g_print("  Link failed: %d\n", ret);
        }
        else
        {
            g_print("  Link succeeded\n");
        }
    }
    else
    {
        g_print("  Not a video pad, ignoring\n");
        gst_object_unref(sink_pad);
    }

    // 释放caps
    if (new_pad_caps != NULL)
    {
        gst_caps_unref(new_pad_caps);
    }
}

static void
set_custom_bbox(NvDsObjectMeta *obj_meta)
{
    guint border_width = 6;
    guint font_size = 18;
    guint x_offset = MIN(SET_WIDTH - 1, (guint)MAX(0, (gint)(obj_meta->rect_params.left - (border_width / 2))));
    guint y_offset = MIN(SET_HEIGHT - 1, (guint)MAX(0, (gint)(obj_meta->rect_params.top - (font_size * 2) + 1)));

    obj_meta->rect_params.border_width = border_width;
    obj_meta->rect_params.border_color.red = 0.0;
    obj_meta->rect_params.border_color.green = 0.0;
    obj_meta->rect_params.border_color.blue = 1.0;
    obj_meta->rect_params.border_color.alpha = 1.0;
    obj_meta->text_params.font_params.font_name = (gchar *)"Ubuntu";
    obj_meta->text_params.font_params.font_size = font_size;
    obj_meta->text_params.x_offset = x_offset;
    obj_meta->text_params.y_offset = y_offset;
    obj_meta->text_params.font_params.font_color.red = 1.0;
    obj_meta->text_params.font_params.font_color.green = 1.0;
    obj_meta->text_params.font_params.font_color.blue = 1.0;
    obj_meta->text_params.font_params.font_color.alpha = 1.0;
    obj_meta->text_params.set_bg_clr = 1;
    obj_meta->text_params.text_bg_clr.red = 0.0;
    obj_meta->text_params.text_bg_clr.green = 0.0;
    obj_meta->text_params.text_bg_clr.blue = 1.0;
    obj_meta->text_params.text_bg_clr.alpha = 1.0;
}

static void
parse_pose_from_meta(NvDsFrameMeta *frame_meta, NvDsObjectMeta *obj_meta)
{

    guint num_joints = obj_meta->mask_params.size / (sizeof(float) * 3);

    gfloat gain = MIN((gfloat)obj_meta->mask_params.width / SET_WIDTH,
                      (gfloat)obj_meta->mask_params.height / SET_HEIGHT);
    gfloat pad_x = (obj_meta->mask_params.width - SET_WIDTH * gain) / 2.0;
    gfloat pad_y = (obj_meta->mask_params.height - SET_HEIGHT * gain) / 2.0;

    NvDsBatchMeta *batch_meta = frame_meta->base_meta.batch_meta;
    NvDsDisplayMeta *display_meta = nvds_acquire_display_meta_from_pool(batch_meta);
    nvds_add_display_meta_to_frame(frame_meta, display_meta);

    for (guint i = 0; i < num_joints; ++i)
    {
        gfloat xc = (obj_meta->mask_params.data[i * 3 + 0] - pad_x) / gain;
        gfloat yc = (obj_meta->mask_params.data[i * 3 + 1] - pad_y) / gain;
        gfloat confidence = obj_meta->mask_params.data[i * 3 + 2];

        if (confidence < 0.5)
        {
            continue;
        }

        if (display_meta->num_circles == MAX_ELEMENTS_IN_DISPLAY_META)
        {
            display_meta = nvds_acquire_display_meta_from_pool(batch_meta);
            nvds_add_display_meta_to_frame(frame_meta, display_meta);
        }

        NvOSD_CircleParams *circle_params = &display_meta->circle_params[display_meta->num_circles];
        circle_params->xc = xc;
        circle_params->yc = yc;
        circle_params->radius = 6;
        circle_params->circle_color.red = 1.0;
        circle_params->circle_color.green = 1.0;
        circle_params->circle_color.blue = 1.0;
        circle_params->circle_color.alpha = 1.0;
        circle_params->has_bg_color = 1;
        circle_params->bg_color.red = 0.0;
        circle_params->bg_color.green = 0.0;
        circle_params->bg_color.blue = 1.0;
        circle_params->bg_color.alpha = 1.0;
        display_meta->num_circles++;
    }

    for (guint i = 0; i < num_joints + 2; ++i)
    {
        gfloat x1 = (obj_meta->mask_params.data[(skeleton[i][0] - 1) * 3 + 0] - pad_x) / gain;
        gfloat y1 = (obj_meta->mask_params.data[(skeleton[i][0] - 1) * 3 + 1] - pad_y) / gain;
        gfloat confidence1 = obj_meta->mask_params.data[(skeleton[i][0] - 1) * 3 + 2];
        gfloat x2 = (obj_meta->mask_params.data[(skeleton[i][1] - 1) * 3 + 0] - pad_x) / gain;
        gfloat y2 = (obj_meta->mask_params.data[(skeleton[i][1] - 1) * 3 + 1] - pad_y) / gain;
        gfloat confidence2 = obj_meta->mask_params.data[(skeleton[i][1] - 1) * 3 + 2];

        if (confidence1 < 0.5 || confidence2 < 0.5)
        {
            continue;
        }

        if (display_meta->num_lines == MAX_ELEMENTS_IN_DISPLAY_META)
        {
            display_meta = nvds_acquire_display_meta_from_pool(batch_meta);
            nvds_add_display_meta_to_frame(frame_meta, display_meta);
        }

        NvOSD_LineParams *line_params = &display_meta->line_params[display_meta->num_lines];
        line_params->x1 = x1;
        line_params->y1 = y1;
        line_params->x2 = x2;
        line_params->y2 = y2;
        line_params->line_width = 6;
        line_params->line_color.red = 0.0;
        line_params->line_color.green = 0.0;
        line_params->line_color.blue = 1.0;
        line_params->line_color.alpha = 1.0;
        display_meta->num_lines++;
    }

    g_free(obj_meta->mask_params.data);
    obj_meta->mask_params.width = 0;
    obj_meta->mask_params.height = 0;
    obj_meta->mask_params.size = 0;
}

static GstPadProbeReturn
tracker_src_pad_buffer_probe(GstPad *pad, GstPadProbeInfo *info, gpointer user_data)
{
    static int frame_count = 0;
    GstBuffer *buf = (GstBuffer *)info->data;
    NvDsBatchMeta *batch_meta = gst_buffer_get_nvds_batch_meta(buf);

    // 一次性打开文件进行追加写入
    std::string filename = *(std::string *)user_data;
    std::ofstream file(filename, std::ios::app);
    if (!file.is_open())
    {
        g_printerr("ERROR: Failed to open file %s for appending\n", filename.c_str());
    }

    NvDsMetaList *l_frame = NULL;
    for (l_frame = batch_meta->frame_meta_list; l_frame != NULL; l_frame = l_frame->next)
    {
        NvDsFrameMeta *frame_meta = (NvDsFrameMeta *)(l_frame->data);

        NvDsMetaList *l_obj = NULL;
        for (l_obj = frame_meta->obj_meta_list; l_obj != NULL; l_obj = l_obj->next)
        {
            NvDsObjectMeta *obj_meta = (NvDsObjectMeta *)(l_obj->data);

            // 获取关键点
            guint num_joints = obj_meta->mask_params.size / (sizeof(float) * 3);

            gfloat gain = MIN((gfloat)obj_meta->mask_params.width / SET_WIDTH,
                              (gfloat)obj_meta->mask_params.height / SET_HEIGHT);
            gfloat pad_x = (obj_meta->mask_params.width - SET_WIDTH * gain) / 2.0;
            gfloat pad_y = (obj_meta->mask_params.height - SET_HEIGHT * gain) / 2.0;
            for (guint i = 0; i < num_joints; ++i)
            {
                gfloat xc = (obj_meta->mask_params.data[i * 3 + 0] - pad_x) / gain;
                gfloat yc = (obj_meta->mask_params.data[i * 3 + 1] - pad_y) / gain;
                gfloat confidence = obj_meta->mask_params.data[i * 3 + 2];

                if (confidence < 0.5)
                {
                    continue;
                }

                // 写入文件
                file << frame_count << "," << obj_meta->object_id << "," << i << "," << xc << "," << yc << "," << confidence << "\n";
            }

            // 绘制骨架和关键点
            parse_pose_from_meta(frame_meta, obj_meta);
            set_custom_bbox(obj_meta);
        }
    }

    // 处理完一帧后增加帧计数
    frame_count++;
    // 关闭文件
    file.close();

    return GST_PAD_PROBE_OK;
}

int main(int argc, char *argv[])
{
    // 初始化管道
    gst_init(&argc, &argv);
    GMainLoop *loop = g_main_loop_new(NULL, FALSE);

    // 创建文件用于保存数据
    std::string csv_filename = getCurrentTimeFilename(".csv");
    // 写入表头
    std::ofstream file(csv_filename);
    if (file.is_open())
    {
        file << "Frame ID,Person ID,Keypoint ID,X,Y,Confidence\n";
        file.close();
    }

    // 管道
    GstElement *pipeline = gst_pipeline_new("test-pipeline");
    if (!pipeline)
    {
        g_printerr("Failed to create pipeline\n");
        return -1;
    }
#if CAPTURE_MODE
    // 创建管道元素
    // filesrc读取文件
    GstElement *source = gst_element_factory_make("filesrc", "file-source");
    if (!source)
    {
        g_printerr("Failed to create source element\n");
        return -1;
    }

    // 设置文件路径
    g_object_set(G_OBJECT(source), "location", VIDEO_PATH, NULL);

    // 添加decoder
    GstElement *decoder = gst_element_factory_make("decodebin", "decoder");
    if (!decoder)
    {
        g_printerr("Failed to create decoder element\n");
        return -1;
    }
#else
    // 创建管道元素
    // v4l2src读取摄像头
    GstElement *source = gst_element_factory_make("v4l2src", "camera-source");
    if (!source)
    {
        g_printerr("Failed to create source element\n");
        return -1;
    }

    // 设置摄像头路径
    g_object_set(G_OBJECT(source), "device", "/dev/video0", NULL);

    // 设置摄像头参数
    GstElement *v4l2caps = gst_element_factory_make("capsfilter", "v4l2capsfilter");

    // 修改caps为摄像头原生RG10格式
    GstCaps *caps = gst_caps_new_simple("video/x-bayer",
                                         "format", G_TYPE_STRING, "rgrg",
                                         "width", G_TYPE_INT, 1920,
                                         "height", G_TYPE_INT, 1080,
                                         "framerate", GST_TYPE_FRACTION, 30, 1,
                                         NULL);
    g_object_set(G_OBJECT(v4l2caps), "caps", caps, NULL);
    gst_caps_unref(caps);

    // 添加RG10到RGB转换器
    GstElement *v4l2bayerconv = gst_element_factory_make("bayer2rgb", "v4l2bayerconv");

    // 添加nvvidconv转换器
    GstElement *v4l2nvvidconv = gst_element_factory_make("nvvidconv", "v4l2nvvidconv");

#endif

    // nvstreammux流复用器
    // 流复用器用于将多个视频流合并为一个流
    GstElement *streammux = gst_element_factory_make("nvstreammux", "stream-muxer");
    if (!streammux)
    {
        g_printerr("Failed to create streammux element\n");
        return -1;
    }

    // 设置流复用器的属性
    g_object_set(G_OBJECT(streammux),
                 "width", SET_WIDTH,
                 "height", SET_HEIGHT,
                 "batch-size", 1,
                 "batched-push-timeout", STREAMMUX_TIMEOUT,
                 NULL);

    // nvinfer推理器
    // 推理器用于对视频帧进行推理
    GstElement *pgie = gst_element_factory_make("nvinfer", "primary-nvinference-engine");
    if (!pgie)
    {
        g_printerr("Failed to create pgie element\n");
        return -1;
    }

    // 设置推理器的属性
    g_object_set(G_OBJECT(pgie),
                 "config-file-path", CONFIG_FILE_PATH,
                 "qos", 0, // 质量服务关闭
                 NULL);

    // nvtracker跟踪器
    // 跟踪器用于对检测到的物体进行跟踪
    GstElement *tracker = gst_element_factory_make("nvtracker", "tracker");
    if (!tracker)
    {
        g_printerr("Failed to create tracker element\n");
        return -1;
    }
    // 设置跟踪器的属性
    g_object_set(G_OBJECT(tracker),
                 "tracker-width", 640,
                 "tracker-height", 384,
                 "ll-lib-file", "/opt/nvidia/deepstream/deepstream/lib/libnvds_nvmultiobjecttracker.so",
                 "ll-config-file", "/opt/nvidia/deepstream/deepstream/samples/configs/deepstream-app/config_tracker_NvDCF_perf.yml",
                 "display-tracking-id", 1,
                 "qos", 0, // 质量服务关闭
                 NULL);

    // nvvidconv视频转换器
    // 视频转换器用于将视频帧转换为适合显示的格式
    GstElement *nvvidconv = gst_element_factory_make("nvvideoconvert", "nvvideoconvert");
    if (!nvvidconv)
    {
        g_printerr("Failed to create nvvidconv element\n");
        return -1;
    }

    // nvdsosd绘制器
    // 绘制器用于在视频帧上绘制检测到的物体
    GstElement *osd = gst_element_factory_make("nvdsosd", "nv-onscreendisplay");
    if (!osd)
    {
        g_printerr("Failed to create osd element\n");
        return -1;
    }
    // 设置绘制器的属性
    g_object_set(G_OBJECT(osd), "process-mode", MODE_GPU, "qos", 0, NULL);

    // 添加元素到管道中并连接数据源和streammux
#if CAPTURE_MODE
    gst_bin_add_many(GST_BIN(pipeline), source, decoder, streammux, pgie, tracker, nvvidconv, osd, NULL);
    if (!gst_element_link_many(source, decoder, NULL))
    {
        g_printerr("Failed to link source and decoder\n");
        return -1;
    }
    // 链接decoder和streammux
    g_signal_connect(decoder, "pad-added", G_CALLBACK(pad_added_handler), streammux);
#else
    gst_bin_add_many(GST_BIN(pipeline), source, v4l2caps, v4l2bayerconv, v4l2nvvidconv, streammux, pgie, tracker, nvvidconv, osd, NULL);
    // 链接source、v4l2caps、v4l2bayerconv和v4l2nvvidconv

    if (!gst_element_link_many(source, v4l2caps, v4l2bayerconv, v4l2nvvidconv, NULL))
    {
        g_printerr("Failed to link source and decoder\n");
        return -1;
    }
    // 链接元素streammux和v4l2nvvidconv
    GstPad *source_src_pad = gst_element_get_static_pad(v4l2nvvidconv, "src");
    GstPad *streammux_sink_pad = gst_element_get_request_pad(streammux, "sink_0");
    if (gst_pad_link(source_src_pad, streammux_sink_pad) != GST_PAD_LINK_OK)
    {
        g_printerr("Failed to link decoder and streammux\n");
        return -1;
    }
    gst_object_unref(source_src_pad);
    gst_object_unref(streammux_sink_pad);
#endif

    // 链接元素
    if (!gst_element_link_many(streammux, pgie, tracker, nvvidconv, osd, NULL))
    {
        g_printerr("Failed to link streammux and other elements\n");
        return -1;
    }

#if VIDEO_SAVE_MODE
    std::string mp4_filename = getCurrentTimeFilename(".mp4");
    // encoder编码器
    // encoder用于将视频流编码为适合存储的格式
    GstElement *encoder = gst_element_factory_make("nvv4l2h264enc", "h264-encoder");
    if (!encoder)
    {
        g_printerr("Failed to create encoder element\n");
        return -1;
    }
    // 设置编码器的属性
    g_object_set(G_OBJECT(encoder), "bitrate", 4000000, "preset-level", 1, NULL);

    // 添加 h264parse 解析器
    GstElement *h264parse = gst_element_factory_make("h264parse", "h264-parser");
    if (!h264parse)
    {
        g_printerr("Failed to create h264parse element\n");
        return -1;
    }

    // mp4mux封装器
    // mp4mux用于将编码后的视频流封装为MP4格式
    GstElement *mp4mux = gst_element_factory_make("mp4mux", "mp4-muxer");
    if (!mp4mux)
    {
        g_printerr("Failed to create mp4mux element\n");
        return -1;
    }

    // sink元素
    // sink元素用于将视频流输出到屏幕或文件
    GstElement *sink = gst_element_factory_make("filesink", "nvvideo-renderer");
    if (!sink)
    {
        g_printerr("Failed to create sink element\n");
        return -1;
    }
    // filesink用于将视频流输出到文件
    g_object_set(G_OBJECT(sink), "location", mp4_filename.c_str(), NULL);

    // 添加视频保存元素
    gst_bin_add_many(GST_BIN(pipeline), encoder, h264parse, mp4mux, sink, NULL);

    // 链接元素元素
    if (!gst_element_link_many(osd, encoder, h264parse, mp4mux, sink, NULL))
    {
        g_printerr("Failed to link streammux and other elements\n");
        return -1;
    }
#else
    // sink元素
    // sink元素用于将视频流输出到屏幕或文件
    GstElement *sink = gst_element_factory_make(VIDEO_SAVE_MODE ? "fakesink" : "nv3dsink",
                                                "nvvideo-renderer");
    if (!sink)
    {
        g_printerr("Failed to create sink element\n");
        return -1;
    }
    // 设置sink元素的属性
    g_object_set(G_OBJECT(sink), "async", 0, "sync", 0, "qos", 0, NULL);

    // 添加sink元素到管道中
    if (!gst_bin_add(GST_BIN(pipeline), sink))
    {
        g_printerr("Failed to add sink element to pipeline\n");
        return -1;
    }
    // 链接元素
    if (!gst_element_link(osd, sink))
    {
        g_printerr("Failed to link osd and sink\n");
        return -1;
    }

#endif

    // 添加消息处理器
    GstBus *bus = gst_element_get_bus(pipeline);
    guint bus_watch_id = gst_bus_add_watch(bus, bus_call, loop);
    gst_object_unref(bus);

    // 添加探针
    GstPad *tracker_src_pad = gst_element_get_static_pad(tracker, "src");
    if (!tracker_src_pad)
    {
        g_printerr("ERROR: Failed to get tracker src pad\n");
        return -1;
    }
    else
        gst_pad_add_probe(tracker_src_pad, GST_PAD_PROBE_TYPE_BUFFER, tracker_src_pad_buffer_probe, &csv_filename, NULL);
    gst_object_unref(tracker_src_pad);

    // 添加性能监控器
    NvDsAppPerfStructInt *perf_struct;
    GstPad *converter_sink_pad = gst_element_get_static_pad(nvvidconv, "sink");
    if (!converter_sink_pad)
    {
        g_printerr("ERROR: Failed to get converter sink pad\n");
        return -1;
    }
    else
    {
        perf_struct = (NvDsAppPerfStructInt *)g_malloc0(sizeof(NvDsAppPerfStructInt));
        enable_perf_measurement(perf_struct, converter_sink_pad, 1, PERF_MEASUREMENT_INTERVAL_SEC, 0, perf_cb);
    }
    gst_object_unref(converter_sink_pad);

    // 启动管道
    gst_element_set_state(pipeline, GST_STATE_PAUSED);

    if (gst_element_set_state(pipeline, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE)
    {
        g_printerr("ERROR: Failed to set pipeline to playing\n");
        return -1;
    }

    // 添加键盘事件监听，用于退出程序
    GIOChannel *io_stdin = g_io_channel_unix_new(fileno(stdin));
    g_io_add_watch(io_stdin, G_IO_IN, key_event, loop);
    g_print("Press ENTER to exit...\n");

    // 运行主循环
    g_print("Running...\n");
    g_main_loop_run(loop);

    // 停止管道并释放资源

    g_print("Stopping...\n");
    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(pipeline);
    g_main_loop_unref(loop);

    return 0;
}
