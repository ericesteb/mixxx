#include "soundsourcemp3.h"

SoundSourceMp3::SoundSourceMp3(const char* filename)
{
    file = fopen(filename,"r");
    if (!file)
    {
        qFatal("MAD: Open of %s failed.", filename);
    }

    // Read the whole file into inputbuf:
    struct stat filestat;
    stat(filename, &filestat);
    inputbuf_len = filestat.st_size;
    inputbuf = new unsigned char[inputbuf_len];
    if (fread(inputbuf,1, inputbuf_len,file) != inputbuf_len)
        qFatal("MAD: Error reading mp3-file.");

    // Transfer it to the mad stream-buffer:
    mad_stream_init(&Stream);
    mad_stream_buffer(&Stream, inputbuf, inputbuf_len);

    /*
      Decode all the headers, and fill in stats:
    */
    int len = 0;
    mad_header Header;
    filelength = mad_timer_zero;
    bitrate = 0;
    pos = mad_timer_zero;

    while ((Stream.bufend - Stream.this_frame) > 0)
    {
        if (mad_header_decode (&Header, &Stream) == -1)
        {
            if (!MAD_RECOVERABLE (Stream.error))
                break;
            if (Stream.error == MAD_ERROR_LOSTSYNC)
            {
                /* ignore LOSTSYNC due to ID3 tags */
                int tagsize = id3_tag_query (Stream.this_frame,
                                Stream.bufend - Stream.this_frame);
                if (tagsize > 0)
                {
                    mad_stream_skip (&Stream, tagsize);
                    continue;
                }
            }

            qDebug("MAD: Error decoding header %d: %s (len=%d)", currentframe, mad_stream_errorstr(&Stream), len);
            continue;
        }
        currentframe++;
        mad_timer_add (&filelength, Header.duration);
        bitrate += Header.bitrate;
        freq = Header.samplerate;
    }

    mad_header_finish (&Header);
    if (currentframe==0)
        bitrate = 0;
    else
        bitrate = bitrate/currentframe;
    framecount = currentframe;
    currentframe = 0;

    /*
    qDebug("length  = %ld sec." , filelength.seconds);
    qDebug("frames  = %d" , framecount);
    qDebug("bitrate = %d" , bitrate/1000);
    qDebug("Size    = %d", length());
    */

    // Re-init buffer:
    mad_stream_finish(&Stream);
    mad_stream_init(&Stream);
    mad_stream_buffer(&Stream, inputbuf, inputbuf_len);
    mad_frame_init(&Frame);
    mad_synth_init(&Synth);

    // Set the type field:
    QString tmp;
    tmp.sprintf("mp3-file.");
    type = tmp;
}

SoundSourceMp3::~SoundSourceMp3()
{
    mad_stream_finish(&Stream);
    mad_frame_finish(&Frame);
    mad_synth_finish(&Synth);
    delete [] inputbuf;
    fclose(file);
}

long SoundSourceMp3::seek(long filepos)
{
    int newpos = inputbuf_len* ((float)filepos/(float)length());
    //qDebug("Seek to %d %d %d", filepos, inputbuf_len, newpos);

    // Go to an approximate position:
    mad_stream_buffer(&Stream, inputbuf+newpos, inputbuf_len-newpos);
    mad_synth_mute(&Synth);
    mad_frame_mute(&Frame);

    // Decode a few (possible wrong) buffers:
    int no = 0;
    int succesfull = 0;
    while ((no<10) && (succesfull<2)) {
        if (!mad_frame_decode(&Frame, &Stream))
            succesfull ++;
        no ++;
    }

    // Discard the first synth:
    mad_synth_frame(&Synth, &Frame);
    // Unfortunately we don't know the exact fileposition. The returned position is thus an
    // approximation only:
    return filepos;
}

/*    FILE *file;
  Return the length of the file in samples.
*/
long unsigned SoundSourceMp3::length()
{
    return (long unsigned) 2*mad_timer_count(filelength, MAD_UNITS_44100_HZ);
}

/*	
  read <size> samples into <destination>, and return the number of
  samples actually read.
*/
unsigned SoundSourceMp3::read(unsigned long samples_wanted, const SAMPLE* _destination)
{
    SAMPLE *destination = (SAMPLE*)_destination;
    unsigned Total_samples_decoded = 0;
    int frames = 0;
    //qDebug("Decoding");
    while (Total_samples_decoded < samples_wanted)
    {
        if(mad_frame_decode(&Frame,&Stream))
        {
            if(MAD_RECOVERABLE(Stream.error))
            {
                qWarning("MAD: Recoverable frame level error (%s)",
                       mad_stream_errorstr(&Stream));
                continue;
            } else if(Stream.error==MAD_ERROR_BUFLEN) {
                qWarning("MAD: buflen error");
                break;
            } else {
                qWarning("MAD: Unrecoverable frame level error (%s).",
                mad_stream_errorstr(&Stream));
                break;
            }
        }
        /* Once decoded the frame is synthesized to PCM samples. No errors
         * are reported by mad_synth_frame();
         */
        mad_synth_frame(&Synth,&Frame);
        frames ++;
        /* Synthesized samples must be converted from mad's fixed
         * point number to the consumer format (16 bit). Integer samples
         * are temporarily stored in a buffer that is flushed when
         * full.
         *
         * the code (MAD_F_FRACBITS-14) used to be (MAD_F_FRACBITS-15). However,
         * this is not enough to avoid out of range when converting to 16 bit
         * (at least on a P3).
         */
        for (int i=0;i<Synth.pcm.length;i++)
        {
             /* Left channel */
            *(destination++) = (SAMPLE)((Synth.pcm.samples[0][i]>>(MAD_F_FRACBITS-14)));

            /* Right channel. If the decoded stream is monophonic then
             * the right output channel is the same as the left one. */
            if (MAD_NCHANNELS(&Frame.header)==2)
                *(destination++) = (SAMPLE)((Synth.pcm.samples[1][i]>>(MAD_F_FRACBITS-14)));
        }
        Total_samples_decoded += 2*Synth.pcm.length;
    }
  
    //qDebug("decoded %i samples in %i frames.", Total_samples_decoded, frames);
    return Total_samples_decoded;
}
