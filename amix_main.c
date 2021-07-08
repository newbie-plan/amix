#include <stdio.h>
#include <string.h>
#include <getopt.h>
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libavutil/rational.h>
#include <libavutil/samplefmt.h>
#include <libavutil/frame.h>


#define MAX_STREAM_NUM 2
#define FRAME_SIZE 1152
#define FILTERS_DESCR "[in0]volume=5dB[a0];[in1]volume=15dB[a1];[a0][a1]amix=inputs=2:duration=first:dropout_transition=3[out]"


typedef struct Parameter
{
    int input_stream_count;
    char *input_file[MAX_STREAM_NUM];
    int input_sample_rate[MAX_STREAM_NUM];
    int input_channels[MAX_STREAM_NUM];
    char *output_file;
    int output_sample_rate;
    int output_channels;
}Parameter;


typedef struct Graph
{
    AVFilterGraph *filter_graph;

    AVFilter *abuffersrc[MAX_STREAM_NUM];
    AVFilterContext *abuffersrc_ctx[MAX_STREAM_NUM];
    AVFilterInOut *outputs[MAX_STREAM_NUM];

    AVFilter *abuffersink;
    AVFilterContext *abuffersink_ctx;
    AVFilterInOut *inputs;
}Graph;


static int init_filters(const char *filters_descr, Graph *graph, Parameter *param)
{
    int i = 0;
    int ret = -1;
    AVFilterInOut *outputs = NULL;
    int sample_rate[2] = {-1};
    int channel_layouts[2] = {-1};
    static const enum AVSampleFormat sample_fmts[] = { AV_SAMPLE_FMT_S16, -1 };

    for (i = 0; i < param->input_stream_count; i++)
    {
        graph->abuffersrc[i] = avfilter_get_by_name("abuffer");
        graph->outputs[i] = avfilter_inout_alloc();
        if (graph->abuffersrc[i] == NULL || graph->outputs[i] == NULL)
        {
            printf("init buffersrc failed.\n");
            exit(0);
        }
    }

    graph->abuffersink = avfilter_get_by_name("abuffersink");
    graph->inputs = avfilter_inout_alloc();
    if (graph->abuffersink == NULL || graph->inputs == NULL)
    {
        printf("init buffersink failed.\n");
        exit(0);
    }

    graph->filter_graph = avfilter_graph_alloc();
    if (graph->filter_graph == NULL)
    {
        printf("avfilter_graph_alloc() failed.\n");
        exit(0);
    }


    for (i = 0; i < param->input_stream_count; i++)
    {
        char name[16] = {0};
        char args[512] = {0};
        snprintf(name, sizeof(name), "in%d", i);
        snprintf(args, sizeof(args), "sample_rate=%d:sample_fmt=%s:channel_layout=0x%"PRIx64,
                 param->input_sample_rate[i], av_get_sample_fmt_name(AV_SAMPLE_FMT_S16), av_get_default_channel_layout(param->input_channels[i]));
        printf("args = [%s]\n", args);

        ret = avfilter_graph_create_filter(&graph->abuffersrc_ctx[i], graph->abuffersrc[i], name, args, NULL, graph->filter_graph);
        if (ret < 0)
        {
            printf("avfilter_graph_create_filter() abuffersrc[%d] failed : [%s]\n", i, av_err2str(ret));
            exit(0);
        }

        graph->outputs[i]->name       = av_strdup(name);
        graph->outputs[i]->filter_ctx = graph->abuffersrc_ctx[i];
        graph->outputs[i]->pad_idx    = 0;
        graph->outputs[i]->next       = NULL;
        if (outputs == NULL)    outputs = graph->outputs[i];
        else                    outputs->next = graph->outputs[i];
    }

    ret = avfilter_graph_create_filter(&graph->abuffersink_ctx, graph->abuffersink, "out", NULL, NULL, graph->filter_graph);
    if (ret < 0)
    {
        printf("avfilter_graph_create_filter() failed : [%s]\n", av_err2str(ret));
        exit(0);
    }

    sample_rate[0] = param->output_sample_rate;
    channel_layouts[0] = av_get_default_channel_layout(param->output_channels);
    ret = av_opt_set_int_list(graph->abuffersink_ctx, "sample_fmts", sample_fmts, -1, AV_OPT_SEARCH_CHILDREN);
    if (ret < 0)
    {
        printf("av_opt_set_int_list() sample_fmts failed : [%s]\n", av_err2str(ret));
        exit(0);
    }
    ret = av_opt_set_int_list(graph->abuffersink_ctx, "channel_layouts", channel_layouts, -1, AV_OPT_SEARCH_CHILDREN);
    if (ret < 0)
    {
        printf("av_opt_set_int_list() channel_layouts failed : [%s]\n", av_err2str(ret));
        exit(0);
    }
    ret = av_opt_set_int_list(graph->abuffersink_ctx, "sample_rates", sample_rate, -1, AV_OPT_SEARCH_CHILDREN);
    if (ret < 0)
    {
        printf("av_opt_set_int_list() sample_rates failed : [%s]\n", av_err2str(ret));
        exit(0);
    }

    graph->inputs->name       = av_strdup("out");
    graph->inputs->filter_ctx = graph->abuffersink_ctx;
    graph->inputs->pad_idx    = 0;
    graph->inputs->next       = NULL;


    ret = avfilter_graph_parse_ptr(graph->filter_graph, filters_descr, &graph->inputs, &outputs, NULL);
    if (ret < 0)
    {
        printf("avfilter_graph_parse_ptr() failed : [%s]\n", av_err2str(ret));
        exit(0);
    }
    ret = avfilter_graph_config(graph->filter_graph, NULL);
    if (ret < 0)
    {
        printf("avfilter_graph_config() failed : [%s]\n", av_err2str(ret));
        exit(0);
    }


    for (i = 0; i < graph->filter_graph->nb_filters; i++)
    {
        printf("[%d] --> filter name = [%s]\n", i, graph->filter_graph->filters[i]->name);
    }

    return 0;
}


static void parse_options(int argc, const char *argv[], Parameter *param)
{
    int optc = -1;
    int opt_index = -1;
    int sample_rate = 0;
    int channels = 0;
    
    while ((optc = getopt(argc, (char *const *)argv, "i:o:r:c:")) != -1)
    {
        switch (optc)
        {
            case 'i':
            {
                param->input_file[param->input_stream_count] = optarg;
                param->input_sample_rate[param->input_stream_count] = sample_rate;
                param->input_channels[param->input_stream_count] = channels;
                param->input_stream_count++;

                sample_rate = 0;
                channels = 0;
                break;
            }
            case 'o':
            {
                param->output_file = optarg;
                param->output_sample_rate = sample_rate;
                param->output_channels = channels;

                sample_rate = 0;
                channels = 0;
                break;
            }
            case 'r':
            {
                sample_rate = atoi(optarg);
                break;
            }
            case 'c':
            {
                channels = atoi(optarg);
                break;
            }
            case '?':
            default:
            {
                printf("error...\n");
                exit(0);
            }
        }
    }
}


int main(int argc, const char *argv[])
{
    int i = 0, ret = -1, eof = 0;
    FILE *fp_o = NULL;
    FILE *fp[MAX_STREAM_NUM] = {0};
    AVFrame *filt_frame = NULL;
    AVFrame *frame[MAX_STREAM_NUM] = {0};
    Parameter param;
    Graph graph;
    memset(&graph, 0, sizeof(Graph));
    memset(&param, 0, sizeof(Parameter));


    parse_options(argc, argv, &param);
    if (param.input_stream_count > MAX_STREAM_NUM)
    {
        printf("only surport [%d] stream mixer\n", MAX_STREAM_NUM);
        return -1;
    }
    printf("input_file[%d] = [%s] : input_sample_rate[%d] = [%d] : input_channels[%d] = [%d]\n\n",
                    0, param.input_file[0], 0, param.input_sample_rate[0], 0, param.input_channels[0]);
    printf("input_file[%d] = [%s] : input_sample_rate[%d] = [%d] : input_channels[%d] = [%d]\n\n",
                    1, param.input_file[1], 1, param.input_sample_rate[1], 1, param.input_channels[1]);
    printf("output_file = [%s] : output_sample_rate = [%d] : output_channels = [%d]\n\n",
                    param.output_file, param.output_sample_rate, param.output_channels);


    for (i = 0; i < param.input_stream_count; i++)
    {
        fp[i] = fopen(param.input_file[i], "rb");
        if (fp[i] == NULL)
        {
            printf("fopen [%s] failed.\n", param.input_file[i]);
            return -1;
        }

        frame[i] = av_frame_alloc();
        if (frame[i] == NULL)
        {
            printf("av_frame_alloc() failed.\n");
            return -1;
        }
        frame[i]->nb_samples = FRAME_SIZE;
        frame[i]->format = AV_SAMPLE_FMT_S16;
        frame[i]->channel_layout = av_get_default_channel_layout(param.input_channels[i]);
        frame[i]->channels = param.input_channels[i];
        frame[i]->sample_rate = param.input_sample_rate[i];

        ret = av_frame_get_buffer(frame[i], 0);
        if (ret < 0)
        {
            printf("av_frame_get_buffer() failed : [%s]\n", av_err2str(ret));
            return -1;
        }
    }
    fp_o = fopen(param.output_file, "wb");
    if (fp_o == NULL)
    {
        printf("fopen [%s] failed.\n", param.output_file);
        return -1;
    }
    filt_frame = av_frame_alloc();
    if (filt_frame == NULL)
    {
        printf("av_frame_alloc() failed.\n");
        return -1;
    }


    init_filters(FILTERS_DESCR, &graph, &param);


    while (1)
    {
        for (i = 0; i < param.input_stream_count; i++)
        {
            if (feof(fp[i]) == 0)
            {
//                int data_size = av_get_bytes_per_sample(frame[i]->format);
                int data_size = av_samples_get_buffer_size(NULL, frame[i]->channels, frame[i]->nb_samples, frame[i]->format, 1);

                fread(frame[i]->data[0], 1, data_size, fp[i]);  /*后续需要考虑planar*/

                ret = av_buffersrc_add_frame_flags(graph.abuffersrc_ctx[i], frame[i], AV_BUFFERSRC_FLAG_KEEP_REF);
                if (ret < 0)
                {
                    printf("av_buffersrc_add_frame_flags() failed : [%s]\n", av_err2str(ret));
                    return -1;
                }
            }
            else
            {
                eof++;          /*mix结束*/
            }
        }


        while (1)
        {
            int data_size = 0;
            ret = av_buffersink_get_frame(graph.abuffersink_ctx, filt_frame);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)   break;
            if (ret < 0)
            {
                printf("av_buffersink_get_frame() failed : [%s]\n", av_err2str(ret));
                return -1;
            }
            data_size = av_samples_get_buffer_size(NULL, filt_frame->channels, filt_frame->nb_samples, filt_frame->format, 1);

            printf("%s ：nb_samples = [%d] : channels = [%d] : format = [%d] sample_rate = [%d] : data_size = [%d]\n", 
                __func__, filt_frame->nb_samples,filt_frame->channels, filt_frame->format, filt_frame->sample_rate, data_size);

            fwrite(filt_frame->data[0], 1, data_size, fp_o);
            av_frame_unref(filt_frame);
        }

        if (eof > 0)    break;  /*结束应该以长的音频流为准,要改*/
    }


    return 0;
}



