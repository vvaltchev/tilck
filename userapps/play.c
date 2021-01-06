/* SPDX-License-Identifier: BSD-2-Clause */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <math.h>

#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>

#include <tilck/common/tilck_sound.h>

static char opt_device[64] = "/dev/sb16";
static char opt_file[64];

static bool opt_test;
static bool opt_file_passed;
static u8 opt_test_short;
static u8 opt_test_bits = 8;
static u8 opt_test_channels = 1;

static void
show_help(void)
{
   printf("syntax:\n");
   printf("    play [-d device] --test [-b 8|16] [-ch 1|2] [-s]\n");
   printf("    play [-d device] <WAVE FILE>\n");
}

static void
show_help_and_exit(void)
{
   show_help();
   exit(1);
}

static void
parse_args(int argc, char **argv)
{
   while (argc) {

      char *arg = argv[0];

      if (!strcmp(arg, "-d")) {

         if (argc < 2)
            show_help_and_exit();

         argc--; argv++;
         strncpy(opt_device, argv[0], sizeof(opt_device)-1);

      } else if (!strcmp(arg, "-h") || !strcmp(arg, "--help")) {

         show_help_and_exit();

      } else if (!strcmp(arg, "--test") || !strcmp(arg, "-t")) {

         opt_test = true;

      } else if (!strcmp(arg, "-s")) {

         if (!opt_test)
            show_help_and_exit();

         opt_test_short = true;

      } else if (!strcmp(arg, "-b")) {

         if (!opt_test)
            show_help_and_exit();

         if (argc < 2)
            show_help_and_exit();

         argc--; argv++;
         int bits = atoi(argv[0]);

         if (bits != 8 && bits != 16)
            show_help_and_exit();

         opt_test_bits = (u8)bits;

      } else if (!strcmp(arg, "-ch")) {

         if (!opt_test)
            show_help_and_exit();

         if (argc < 2)
            show_help_and_exit();

         argc--; argv++;
         int channels = atoi(argv[0]);

         if (channels != 1 && channels != 2)
            show_help_and_exit();

         opt_test_channels = (u8)channels;

      } else {

         if (opt_test)
            show_help_and_exit();

         opt_file_passed = true;
         strncpy(opt_file, argv[0], sizeof(opt_file)-1);
         break;
      }

      argc--; argv++;
   }

   if (!opt_test && !opt_file_passed)
      show_help_and_exit();
}

static u32
time_to_samples(u32 rate, u32 time_ms)
{
   if (time_ms == 0)
      return 0;

   return time_ms < 1000
      ? 1000 * rate / (1000000 / time_ms)
      : rate * time_ms / 1000;
}

static void
tone_ll(void *raw_buf,
        u32 rate,
        float freq,
        u32 start,
        u32 cnt,
        u8 bits,
        u8 channels,
        u8 channel_n)
{
   const u32 u = (1 << (bits - 1)) - 1;
   const double volume = 0.75;
   const u32 end = start + cnt;

   s8 *buf8 = raw_buf;
   s16 *buf16 = raw_buf;
   s32 *buf32 = raw_buf;

   for (u32 i = start; i < end; i++) {

      double t = (double)(i - start) / ((double)rate / freq);
      int val = (int)(sin(t * 2.0 * M_PI) * volume * u);

      if (channels == 1) {

         if (bits == 8)
            buf8[i] = (u8)val;
         else
            buf16[i] = (u16)val;

      } else {

         if (bits == 8) {

            if (channel_n != 255)
               buf16[i] = ((u16)val << 8 * channel_n);
            else
               buf16[i] = ((u16)val | val << 8);

         } else {

            if (channel_n != 255)
               buf32[i] = ((u16)val << 16 * channel_n);
            else
               buf32[i] = ((u16)val | val << 16);
         }
      }
   }
}

static void
tone(void *raw_buf,
     u32 rate,
     float freq,
     u32 start_ms,
     u32 duration_ms,
     u8 bits,
     u8 channels,
     u8 channel_n)
{
   const u32 start = time_to_samples(rate, start_ms);
   const u32 cnt = time_to_samples(rate, duration_ms);
   tone_ll(raw_buf, rate, freq, start, cnt, bits, channels, channel_n);
}

static u32
gen_test_sound(void *buf, u32 max_samples, u8 bits, u8 channels, u32 rate)
{
   const float freq = 326.0;
   u32 ms = 0;

   /* Fill the whole buffer with "mute" sound */
   memset(buf, 0, max_samples * (bits >> 3) * channels);

   if (!opt_test_short) {

      tone(buf, rate, freq  ,   ms, 1486, bits, channels, 0);
      ms += 1486;

      tone(buf, rate, freq/2,   ms, 1486, bits, channels, 1);
      ms += 1486;

   } else {

      tone(buf, rate, freq  ,   ms, 100, bits, channels, 0);
      ms += 100;
   }

   return time_to_samples(rate, ms);
}

static int
test_sound(int devfd)
{
   const u32 max_samples = 64 * KB;
   u32 tot_sz = max_samples * (opt_test_bits >> 3) * opt_test_channels;

   void *raw_buf = malloc(tot_sz);
   int rc, written = 0;

   if (!raw_buf) {
      printf("Out of memory\n");
      return 1;
   }

   printf("Playing test sound: %u bits, %u channels at 22050 Hz\n",
          opt_test_bits, opt_test_channels);

   struct tilck_sound_params p = {
      .sample_rate = 22050,
      .bits = opt_test_bits,
      .channels = opt_test_channels,
      .sign = 1,
   };

   tot_sz = gen_test_sound(
      raw_buf, max_samples, p.bits, p.channels, p.sample_rate
   );

   tot_sz *= (opt_test_bits >> 3) * opt_test_channels;
   rc = ioctl(devfd, TILCK_IOCTL_SOUND_SETUP, &p);

   if (rc < 0) {
      printf("Sound device setup ioctl() failed: %s\n", strerror(errno));
      return 1;
   }

   if (!opt_test_short) {

      do {

         rc = write(devfd, (char *)raw_buf + written, tot_sz - written);

         if (rc < 0) {
            printf("write() on sound device failed: %s\n", strerror(errno));
            break;
         }

         written += rc;

      } while (written < tot_sz);

   } else {

      write(devfd, (char *)raw_buf, tot_sz);
   }

   free(raw_buf);
   return 0;
}

#define CHUNK_ID_RIFF       0x52494646
#define FORMAT_WAV          0x57415645
#define SUBCHUNK1_ID_FMT    0x666d7420
#define SUBCHUNK2_ID_DATA   0x64617461
#define AUDIO_FORMAT_PCM    0x00000001

struct wav_header {

   u32 ChunkID;
   u32 ChunkSize;
   u32 Format;
   u32 Subchunk1ID;
   u32 Subchunk1Size;
   u16 AudioFormat;
   u16 NumChannels;
   u32 SampleRate;
   u32 ByteRate;
   u16 BlockAlign;
   u16 BitsPerSample;
   u32 Subchunk2ID;
   u32 Subchunk2Size;
};

static u32 be32(u32 x)
{
   return ((x >> 24) & 0x000000ff) |
          ((x <<  8) & 0x00ff0000) |
          ((x >>  8) & 0x0000ff00) |
          ((x << 24) & 0xff000000);
}

static int
check_wav_header(struct wav_header *hdr)
{
   if (be32(hdr->ChunkID) != CHUNK_ID_RIFF) {
      printf("Not a WAV file: wrong ChunkID\n");
      return -1;
   }

   if (be32(hdr->Format) != FORMAT_WAV) {
      printf("Not a WAV file: wrong Format\n");
      return -1;
   }

   if (be32(hdr->Subchunk1ID) != SUBCHUNK1_ID_FMT) {
      printf("Not a WAV file: wrong Subchunk1ID\n");
      return -1;
   }

   if (be32(hdr->Subchunk2ID) != SUBCHUNK2_ID_DATA) {
      printf("Not a WAV file: wrongSubchunk2ID\n");
      return -1;
   }

   if (hdr->AudioFormat != AUDIO_FORMAT_PCM || hdr->Subchunk1Size != 16) {
      printf("Audio format not supported\n");
      return -1;
   }

   if (hdr->NumChannels != 1 && hdr->NumChannels != 2) {
      printf("Unsupported number of channels: %u\n", hdr->NumChannels);
      return -1;
   }

   if (hdr->BitsPerSample != 8 && hdr->BitsPerSample != 16) {
      printf("Unsupported bits per sample: %u\n", hdr->BitsPerSample);
      return -1;
   }

   if (hdr->ChunkSize - 36 > hdr->Subchunk2Size) {
      printf("Multiple subchunks in the WAV file are not supported\n");
      return -1;
   }

   u32 exp_br = hdr->SampleRate * hdr->NumChannels * hdr->BitsPerSample / 8;

   if (hdr->ByteRate != exp_br) {
      printf("ByteRate %u != expected value %u\n", hdr->ByteRate, exp_br);
      return -1;
   }

   return 0;
}

static int
play_wav_file(int devfd)
{
   int fd = open(opt_file, O_RDONLY);
   u32 tot_read = 0, data_read;
   u32 last_sec = (u32) -1;
   struct wav_header hdr;
   u8 *buf = NULL;
   int rc = 0;

   if (fd < 0) {
      printf("Unable to open WAV file '%s': %s\n", opt_file, strerror(errno));
      return 1;
   }

   rc = read(fd, &hdr, sizeof(hdr));

   if (rc < sizeof(hdr)) {
      printf("Invalid file header\n");
      goto out;
   }

   if ((rc = check_wav_header(&hdr)) < 0)
      goto out;

   printf("Playing file '%s'...\n", opt_file);
   printf("%u bits/sample, %u channels at %u Hz\n",
          hdr.BitsPerSample, hdr.NumChannels, hdr.SampleRate);

   buf = malloc(32 * KB);

   if (!buf) {
      printf("Out of memory\n");
      rc = 1;
      goto out;
   }

   struct tilck_sound_params params = {
      .sample_rate = hdr.SampleRate,
      .bits = hdr.BitsPerSample,
      .channels = hdr.NumChannels,
      .sign = 1,
   };

   rc = ioctl(devfd, TILCK_IOCTL_SOUND_SETUP, &params);

   if (rc < 0) {
      printf("Unable to setup sound device: %s\n", strerror(errno));
      goto out;
   }

   do {

      const u32 sec = tot_read / hdr.ByteRate;
      const u32 tot_sec = hdr.Subchunk2Size / hdr.ByteRate;
      u32 written = 0;
      data_read = 0;

      if (sec != last_sec) {
         printf("\033[2K\033[G");
         printf("Time: %02u:%02u / %02u:%02u",
                sec / 60, sec % 60, tot_sec / 60, tot_sec % 60);
         fflush(stdout);
         last_sec = sec;
      }

      /* Read 32 KB */
      while ((rc = read(fd, buf + data_read, 32 * KB - data_read)) > 0) {
         data_read += rc;
      }

      if (rc < 0) {
         printf("\nread() on WAV file failed with: %s\n", strerror(errno));
         goto out;
      }

      while (written < data_read) {

         rc = write(devfd, buf + written, data_read - written);

         if (rc < 0) {
            printf("\nwrite() on sound device failed: %s\n", strerror(errno));
            goto out;
         }

         written += rc;
      }

      tot_read += data_read;

   } while (data_read == 32 * KB);

   printf("\n");
   rc = 0;

out:
   free(buf);
   close(fd);
   return !!rc;
}

static int
do_run_cmd(int devfd)
{
   if (opt_test) {

      return test_sound(devfd);

   } else if (opt_file_passed) {

      return play_wav_file(devfd);
   }

   show_help();
   return 1;
}

static void
show_sb16_message_warning(void)
{
   printf("-------------------------- WARNING --------------------------\n");
   printf("The regular QEMU GTK UI doesn't work well with the emulation\n");
   printf("of the SoundBlaster 16 sound card: while the sound is playing\n");
   printf("you'll observe the whole QEMU window freezing AND flicker\n");
   printf("in the sound as well.\n\n");
   printf("QEMU bug: https://bugs.launchpad.net/qemu/+bug/1873769\n");
   printf("\n");
   printf("Workarounds (alternatives):\n\n");

   printf("  1. Best: use virt-manager and create a Tilck VM with the sb16\n");
   printf("     sound card. To do that it's necessary to manually edit\n");
   printf("     the XML config for the sound device. Make it be:\n");
   printf("          <sound model=\"sb16\"/>\n");
   printf("     Note: in this case it's possible to use the framebuffer\n");
   printf("     console while playing sound, without anything freezing.\n\n");

   printf("  2. Use an older QEMU like 2.11. As good as solution 1.\n\n");

   printf("  3. Run QEMU with -curses and boot in text mode (some flicker,\n");
   printf("     but no freezing).\n\n");

   printf("  4. Run QEMU in nographic mode (as solution 3):\n");
   printf("     ./build/run_multiboot_qemu -nographic -append -sercon\n");
   printf("-------------------------------------------------------------\n");
}

int main(int argc, char **argv)
{
   int rc, cmd_rc, devfd;
   struct tilck_sound_card_info nfo;

   parse_args(argc-1, argv+1);

   /* Initialization */
   devfd = open(opt_device, O_WRONLY);

   if (devfd < 0) {
      printf("Failed to open device: %s\n", opt_device);
      return 1;
   }

   rc = ioctl(devfd, TILCK_IOCTL_SOUND_ACQUIRE, NULL);

   if (rc < 0) {
      printf("Failed to acquire the sound device\n");
      return 1;
   }

   rc = ioctl(devfd, TILCK_IOCTL_SOUND_GET_INFO, &nfo);

   if (rc < 0) {
      printf("Failed to get info for the sound device\n");
      return 1;
   }

   if (!strcmp(nfo.name, "sb16")) {

      /* QEMU >= 4.0 has a nasty bug affecting this sound card */
      show_sb16_message_warning();

      /* Give QEMU some time to refresh the video, before start playing */
      usleep(200 * 1000);
   }

   /* Run the main program logic */
   cmd_rc = do_run_cmd(devfd);

   /* Finalization */
   rc = ioctl(devfd, TILCK_IOCTL_SOUND_WAIT_COMPLETION, NULL);

   if (rc < 0) {
      printf("Failed to wait for completion\n");
      return 1;
   }

   rc = ioctl(devfd, TILCK_IOCTL_SOUND_RELEASE, NULL);

   if (rc < 0) {
      printf("Failed to release the sound device\n");
      return 1;
   }

   close(devfd);
   return cmd_rc;
}
