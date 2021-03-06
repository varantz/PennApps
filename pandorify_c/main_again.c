#include "appkey.c"
// #include "mongo.h"

#define USER_AGENT "pandorify"

#include <assert.h>
#include <pthread.h>
#include <sys/time.h>
#include <libgen.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <libspotify/api.h>
#include <strings.h>

char filename[256];
// Instead of mutex's we just busy-wait

int g_track_updated = 0;
int g_playlist_added = 0;
int g_logged_in = 0;
int g_container_loaded = 0;
int g_playlist_created = 0;

// Playlist callbacks

void tracks_added(sp_playlist *pl, sp_track *const *tracks, int num_tracks, 
				  int position, void *userdata)
{
  printf("tracks added\n");
  g_track_updated = 1;
}

sp_playlist_callbacks playlist_callbacks = {
  .tracks_added = tracks_added,
};

// Playlist container callbacks

static void playlist_added(sp_playlistcontainer *pc, sp_playlist *pl,
                           int position, void *userdata)
{
  printf("playlist added.\n");
  // Add a playlist callback so we know when the track's been added later
  sp_playlist_add_callbacks(pl, &pl_callbacks, NULL);
  g_playlist_created = 1;
}

static void container_loaded(sp_playlistcontainer *pc, void *userdata)
{
  printf("Container loaded.\n");
  g_container_loaded = 1;
}

static void playlist_added(sp_playlistcontainer *pc, sp_playlist *pl,
                           int position, void *userdata)
{
  printf("playlist_added callback for %s.\n", sp_playlist_name(pl));
  g_playlist_added = 1 ? g_container_loaded : 0;
}

static sp_playlistcontainer_callbacks pc_callbacks = {
  .playlist_added = &playlist_added,
  .container_loaded = &container_loaded,
};

// Session callbacks

static void connection_error(sp_session *session, sp_error error)
{
  fprintf(stderr, "Connection error\n");
}

static void logged_in(sp_session *session, sp_error error)
{
    printf("In login.\n");
    if (error != SP_ERROR_OK) {
        fprintf(stderr, "Error: unable to log in: %s\n", sp_error_message(error));
        exit(1);
    }
    printf("Logged in success!\n");
	
	// Add callbacks for loading
	sp_playlistcontainer *pc = sp_session_playlistcontainer(session);
	sp_playlistcontainer_add_callbacks(pc, &pc_callbacks, NULL);

	// Allow rest of main to run
	g_logged_in = 1;
}

static void logged_out(sp_session *session)
{
  printf("Logging out and exiting.\n");
  exit(0);
}

static void log_message(sp_session *session, const char *data)
{
  fprintf(stderr,"%s",data);
}

static void notify_main_thread(sp_session *session)
{
  printf("main thread notified.\n");
}

static sp_session_callbacks callbacks = {
  .logged_in = &logged_in,
  .logged_out = &logged_out,
  .notify_main_thread = &notify_main_thread,
  .log_message = &log_message,
};

static sp_session_config config = {
    .api_version = SPOTIFY_API_VERSION,
    .cache_location = "tmp",
    .settings_location = "tmp",
    .application_key = g_appkey,
    .application_key_size = 0,
    .user_agent = USER_AGENT,
    .callbacks = &callbacks,
	NULL,
};

void list_playlists(sp_session *session) {
  sp_playlistcontainer *pc = sp_session_playlistcontainer(session);
  int next_timeout = 0;
  printf("waiting for container to load.\n");
  
  while (!g_container_loaded) 
	{
	  sp_session_process_events(session, &next_timeout);
	  printf("sleeping b/c container not loaded yet.\n");
	  usleep(next_timeout * 1000);
	}
  printf("container successfully loaded.\n");
  g_container_loaded = 0;
  int i, j, level = 0;
  sp_playlist *pl;
  char name[200];
  int new = 0;
  
  printf("%d playlists available\n", sp_playlistcontainer_num_playlists(pc));

  for (i = 0; i < sp_playlistcontainer_num_playlists(pc); ++i) {
    switch (sp_playlistcontainer_playlist_type(pc, i)) {
    case SP_PLAYLIST_TYPE_PLAYLIST:
      printf("%d. ", i);
      for (j = level; j; --j) printf("\t");
      pl = sp_playlistcontainer_playlist(pc, i);
      printf("%s", sp_playlist_name(pl));
      printf("\n");
      break;
    case SP_PLAYLIST_TYPE_START_FOLDER:
      printf("%d. ", i);
      for (j = level; j; --j) printf("\t");
      sp_playlistcontainer_playlist_folder_name(pc, i, name, sizeof(name));
      printf("Folder: %s with id %zu\n", name,
	  sp_playlistcontainer_playlist_folder_id(pc, i));
      level++;
      break;
    case SP_PLAYLIST_TYPE_END_FOLDER:
      level--;
      printf("%d. ", i);
      for (j = level; j; --j) printf("\t");
      printf("End folder with id %zu\n",	sp_playlistcontainer_playlist_folder_id(pc, i));
      break;
    case SP_PLAYLIST_TYPE_PLACEHOLDER:
      printf("%d. Placeholder", i);
      break;
    }
  }
}

void omega(sp_session *session, char *filename) {
  printf("entering omega.\n");
  sp_playlistcontainer *pc = sp_session_playlistcontainer(session);
  int next_timeout = 0;
  printf("waiting for container to load.\n");

  while (!g_container_loaded) 
	{
	  sp_session_process_events(session, &next_timeout);
	  printf("sleeping\n");
	  usleep(next_timeout * 1000);
	}
  printf("container successfully loaded.\n");

  g_container_loaded = 0;
  int n = 512;
  char buf[n];
  int make_new_playlist = 0;
  sp_playlist *pl = NULL;
  FILE *fp = fopen(filename, "r");
  
  // Go through the file
  while (fgets(&buf, n, fp))
	{
	  printf("Processing line in file.\n");
	  int len = strnlen(buf, n);
	  buf[len-1] = '\0';
	  if (make_new_playlist) {
		printf("Making new playlist: %s.\n", buf);
		g_playlist_created = 0;
		pl = sp_playlistcontainer_add_new_playlist(pc, buf);

		// Wait for update
		while (!g_playlist_created) 
		  {
			sp_session_process_events(session, &next_timeout);
			printf("sleeping\n");
			usleep(next_timeout * 1000);
		  }
		if (!pl) printf("Weird, the callback must have fired but pl is null.\n");
		// Callbacks are added in create callback
	  }
	  else if (!strncmp(&buf, "---", 3))
		{
		  make_new_playlist = 1;
		  printf("Found playlist delimiter.\n");
		}
	  else {
		// Means we are adding a track
		printf("Adding track %s to playlist %s.\n", buf, sp_playlist_name(pl));
		// Create the track object
		sp_link *track_link = NULL;
		track_link = sp_link_create_from_string(buf);
		if (track_link == NULL || sp_link_type(track_link) != SP_LINKTYPE_TRACK) {
		  printf("%s does not represent a valid link.\n", buf);
		  continue;
		}
		sp_track *track = sp_link_as_track(track_link);
		if (track == NULL) {
		  printf("Weird, we checkd for null already.\n");
		  continue;
		}
		while (!sp_track_is_loaded(track)) {
		  sp_session_process_events(session, &next_timeout);
		  printf("sleeping.\n");
		  usleep(next_timeout * 1000);
		}
		printf("Track is loaded.\n");
		// Add to the playlist 
		g_track_added = 0;
		sp_error error = sp_playlist_add_tracks(pl, &track, 1, 0, session);
		if (SP_ERROR_OK != error) {
		  fprintf(stderr, "failed to add track: %s\n",
				  sp_error_message(error));
		  continue; // let's try and add the rest anyway
		}
		while (!g_track_added) {
		  sp_session_process_events(session, &next_timeout);
		  printf("sleeping.\n");
		  usleep(next_timeout * 1000);
		}
		printf("Track is added.\n");
		g_track_added = 0;
	  }
	}
}

void pandorify(sp_session *session, char *filename) {
  printf("Pandorify'ing.\n");
  sp_playlistcontainer *pc = sp_session_playlistcontainer(session);
  int next_timeout = 0;
  while (!g_container_loaded) 
	{
	  sp_session_process_events(session, &next_timeout);
	  printf("sleeping b/c container not loaded yet.\n");
	  usleep(next_timeout * 1000);
	}
  printf("%d playlists available.\n", sp_playlistcontainer_num_playlists(pc));
  
  // The real shit
  char buff[512];
  char *buff_const;
  FILE *fp = fopen(filename, "r");
  int make_new_playlist = 0;
  sp_playlist *pl = NULL;

  while (buff_const = fgets(&buff, 512, fp))
	{
	  if (!buff_const) { 
		printf("Error reading file.\n");
		break; 
	  }
	  assert (buff_const == &buff);

	  int len = strnlen(buff_const, 512);
	  buff[len-1] = '\0';

	  if (!strncmp(buff_const, "---", 3)) {
		make_new_playlist = 1;
	  }
	  else if (make_new_playlist) {
		make_new_playlist = 0;
		printf("Will make a new playlist here called %s.\n", buff_const);
		pl = sp_playlistcontainer_add_new_playlist(pc, buff_const);

		while (!g_playlist_added) {
		  sp_session_process_events(session, &next_timeout);
		  printf("sleeping b/c playlist not ready.\n");
		  usleep(next_timeout * 1000);
		}
		// We are done waiting
		g_playlist_added = 0;
	  }
	  else {
		printf("Will make new track %s to playlist %s.\n", buff_const, sp_playlist_name(pl));
		sp_link *link = sp_link_create_from_string(buff_const);
		sp_track *track = sp_link_as_track(link);
		int next_timeout2 = 0;
		sp_error error;
		while (error = sp_track_error(track) == SP_ERROR_IS_LOADING) {
		  sp_session_process_events(session, &next_timeout2);
		  printf("sleeping b/c playlist not ready.\n");
		  usleep(next_timeout * 1000);
		}
		if (SP_ERROR_OK != error) {
		  fprintf(stderr, "failed to load correct tracks: %s\n",
				  sp_error_message(error));
		}
		else {
		  sp_playlist_add_tracks(pl, &track, 1, 0, session);
		}
	  }
	}
}

int main(int argc, char *argv[])
{
    sp_error error;
    sp_session *session;
	if (argc < 4) {
	  printf("Not enough arguments.\n");
	  exit(1);
	}
	char *username = argv[1];
	char *password = argv[2];
	char *filename = argv[3];
    config.application_key_size = g_appkey_size;
    error = sp_session_create(&config, &session);
    if (error != SP_ERROR_OK) {
        fprintf(stderr, "Error: unable to create spotify session: %s\n", sp_error_message(error));
        return 1;
    }
    int next_timeout = 0;
	
    sp_session_login(session, username, password, 0, NULL);
    while (!g_logged_in) {
        sp_session_process_events(session, &next_timeout);
		printf("sleeping b/c not logged in yet.\n");
        usleep(next_timeout * 1000);
    }
	// list_playlists(session);
	pandorify(session, filename);
	error = sp_session_logout(session);
	if (error != SP_ERROR_OK) {
        fprintf(stderr, "Error: unable to logout spotify session: %s\n", sp_error_message(error));
        return 1;
    }
    return 0;
}
