#include <stdio.h>
#include <sndfile.h>
#include <math.h>
#include <stdint.h> 
//#include <ncurses.h>
#include <sys/wait.h>
#include <spawn.h>
struct ad { 
	double start;
	double end; 
	double temp;
};


long int seconds_to_frames(SF_INFO *sfinfo, int seconds);
int read_file(float *floats_array, struct ad *timestmaps);
double rms(int total_samples, float *floats_array);
void start_ad(float *time_elapsed, int *ad_running, double dbfs, int *last_index, struct ad *timestamps);
void end_ad(float *time_elapsed, int *ad_running, double dbfs, int *last_index, struct ad *timestamps);
struct get_time_struct { 
	int hours; 
	int minutes;
	int seconds;		
};

struct get_time_struct get_time(int seconds);
int play_clip(const char *filename, int start_sec, int end_sec);
struct init_vars_struct{
	const char *file_path; 
	SNDFILE *audio_file;
	double sample_rate;
	float chunk_size;
	float least_count;
	int total_samples;
	uint64_t total_frames_processed;
	double offset;
	double read_till_time; 
	double current_time_seconds;
	sf_count_t frames;
	long int frames_to_read;
	int channels;
};
struct init_vars_struct init_vars;

int main() {
	//init start
	SF_INFO sfinfo;


	init_vars.file_path = "/your_path_here/audio.flac"; //path to the audio file (flac format)
	init_vars.audio_file = sf_open(init_vars.file_path, SFM_READ, &sfinfo);
	init_vars.sample_rate = sfinfo.samplerate;
	init_vars.chunk_size = init_vars.sample_rate / 100; //timing precision 0.01 seconds
	init_vars.least_count = init_vars.chunk_size / init_vars.sample_rate;
	init_vars.total_samples = init_vars.chunk_size * sfinfo.channels;
	init_vars.total_frames_processed = 0;
	init_vars.offset = 5674; //time (seconds) from which to start checking for ads (beginning of innings)
	init_vars.current_time_seconds = init_vars.offset;
	init_vars.frames = seconds_to_frames(&sfinfo,(int)init_vars.current_time_seconds);
	init_vars.frames_to_read = 500000; //useless
	init_vars.read_till_time = 18993; // time (seconds) till which to check for ads (end of innings)
	init_vars.channels = sfinfo.channels;
	sf_seek(init_vars.audio_file, init_vars.frames, SEEK_SET);
	float floats_array[init_vars.total_samples];
	//init end

	
	struct ad timestamps[1500] = {0};
	
	read_file(floats_array, timestamps);
	//char *video_file_path = "match.mkv";
	int f = 0;
	//file handling 
	FILE *fp = fopen("cuts.ffconcat", "w");
	if (fp == NULL) {
		printf("error\n");
    		perror("fopen");
    		getchar();
	}	
	fputs("ffconcat version 1.0\n\n", fp);
	while(timestamps[f].start != 0 && f < 1500) { 
		printf("Ad from %f to %f\n", timestamps[f].start, timestamps[f].end);
		if (f == 0) { 
			fputs("file 'in.mkv'\n", fp);
			fprintf(fp, "outpoint %f\n\n", timestamps[f].start);
		} else { 
			fputs("file 'in.mkv'\n", fp);
			fprintf(fp, "inpoint %f\n", timestamps[f-1].end);
			fprintf(fp, "outpoint %f\n\n", timestamps[f].start);
		}
		//printf("Press ENTER to play this clip\n");
		//getchar();
		//play_clip(video_file_path, timestamps[f].start - 0.5, timestamps[f].end + 0.5);
		f++;
	}
	fputs("file 'in.mkv'\n", fp);
	fprintf(fp, "inpoint %f\n", timestamps[f-1].end);
	
	struct get_time_struct get_init_time = get_time(init_vars.offset);
	struct get_time_struct get_final_time = get_time(init_vars.current_time_seconds);


	printf("-----\nread file from %d:%d:%d to %d:%d:%d\n-----\n", get_init_time.hours, get_init_time.minutes, get_init_time.seconds, get_final_time.hours, get_final_time.minutes, get_final_time.seconds);
}

int read_file(float *floats_array, struct ad *timestamps) {
	int i; 
	float time_elapsed = 0;
	int ad_running = 0;
	int last_index = 0;
	while(init_vars.current_time_seconds <= init_vars.read_till_time) { 
		sf_count_t frames_read = sf_readf_float(init_vars.audio_file, floats_array, init_vars.chunk_size);
		double dbfs = rms(frames_read * init_vars.channels, floats_array);
		if (ad_running == 0) { 
			start_ad(&time_elapsed, &ad_running, dbfs, &last_index, timestamps);
		} else { 
			end_ad(&time_elapsed, &ad_running, dbfs, &last_index, timestamps);
		}
		init_vars.current_time_seconds += (double)frames_read / init_vars.sample_rate;
	}
	return 0;
}

double rms(int total_samples, float *floats_array){
	double floats_square_sum = 0.0;
	int i;
	for (i = 0; i < total_samples; i++) { 
		floats_square_sum += floats_array[i] * floats_array[i];
	}
	double rms_value = sqrt(floats_square_sum / total_samples);
	double dbfs;
	if (rms_value < 0.000001) { //to catch infinity errors
		dbfs = -100;
	} else { 
		dbfs = 20 * log10(rms_value);
	}
	//printf("%f dbfs: \n", dbfs);
	return dbfs;
}

void start_ad(float *time_elapsed, int *ad_running, double dbfs, int *last_index, struct ad *timestamps) { 
	if (dbfs < -90) {
		//printf("current time: %f\n", init_vars.current_time_seconds);
		*time_elapsed += init_vars.least_count;

	} else if (dbfs >= -90 && *time_elapsed <= 6.2 && *time_elapsed > 0) { //ad starting may be detected	

		if ((init_vars.current_time_seconds - timestamps[*last_index - 1].end) < 51) { //last ad end was a false alarm
			timestamps[*last_index - 1].end = init_vars.current_time_seconds;
			*time_elapsed = 0; 

		} else if (((init_vars.current_time_seconds - timestamps[*last_index - 1].end) >= 51) && *time_elapsed <= 1) { //ad starting actually detected
			*ad_running = 1;
			*time_elapsed = 0;
			timestamps[*last_index].start = init_vars.current_time_seconds; 
		}
	} else if (dbfs >= -90 && *time_elapsed > 1 && *time_elapsed >= 0) {
		*ad_running = 0;
		*time_elapsed = 0;
	} else {
		//printf("else");
	}
}

void end_ad(float *time_elapsed, int *ad_running, double dbfs, int *last_index, struct ad *timestamps) { 
	if (dbfs < -90) { 
		//printf("time : %f\n", init_vars.current_time_seconds);
		*time_elapsed += init_vars.least_count;
	} else if (dbfs >= -90 && *time_elapsed <= 6.2 && *time_elapsed > 0) { //ad ending has been detected 
		*ad_running = 0;
		*time_elapsed = 0;
		timestamps[*last_index].end = init_vars.current_time_seconds;
		(*last_index)++;

	} else if (dbfs >= -90 && *time_elapsed > 6.2) { 
		*time_elapsed = 0;
	}
}



long int seconds_to_frames(SF_INFO *sfinfo, int seconds){ 
	int samplerate = sfinfo->samplerate;
	unsigned long long int frames = samplerate*seconds;
	printf("offset is: %lld frames, %d seconds\n", frames, seconds);
	return frames;
}

struct get_time_struct get_time(int seconds) { 
	int hour = (seconds - (seconds % (60*60))) / (60*60);
	seconds = seconds % (60*60);
	int minute = (seconds - (seconds % 60)) / 60;
	seconds = seconds % 60;

	return (struct get_time_struct){ .hours = hour, .minutes = minute, .seconds = seconds };
}

/*
extern char **environ;

int play_clip(const char *filename, int start_sec, int end_sec)
{
    pid_t pid;

    char start_arg[32];
    char end_arg[32];

    snprintf(start_arg, sizeof(start_arg), "--start=%d", start_sec);
    snprintf(end_arg, sizeof(end_arg), "--end=%d", end_sec);

    char *argv[] = {
        "mpv",
        start_arg,
        end_arg,
        (char *)filename,
        NULL
    };

    int status = posix_spawnp(&pid, "mpv", NULL, NULL, argv, environ);
    if (status != 0)
        return status;

    waitpid(pid, NULL, 0);

    return 0;
}*/
