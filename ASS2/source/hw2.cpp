#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <termios.h>
#include <fcntl.h>

/* const numbers define */
#define ROW 17
#define COLUMN 49
#define HORI_LINE '-'
#define VERT_LINE '|'
#define CORNER '+'
#define PLAYER '0'

#define NUM_OF_WALL 6
#define WALL_LENGTH 15
#define NUM_OF_GOLD 6

#define WALL '='
#define GOLD '$'

/* global variables */
int player_x;
int player_y;
char map[ROW][COLUMN + 1]; // why is it +1?

pthread_mutex_t mutex;  // mutex for protecting shared resources
int stop_game = 0;      // game control flag

// Position structure
struct Position {
    int x, y;
};

struct Position wall[NUM_OF_WALL];  // wall positions

/* functions */
int kbhit(void);
void map_print(void);
void init_walls(void);
void move_wall(int index, int direction);
void *wall_move(void *arg);
void *auto_refresh(void *arg);

/* Determine a keyboard is hit or not.
 * If yes, return 1. If not, return 0. */
int kbhit(void)
{
    struct termios oldt, newt;
    int ch;
    int oldf;
    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;
    newt.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);
    oldf = fcntl(STDIN_FILENO, F_GETFL, 0);
    fcntl(STDIN_FILENO, F_SETFL, oldf | O_NONBLOCK);
    ch = getchar();
    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    fcntl(STDIN_FILENO, F_SETFL, oldf);
    if (ch != EOF)
    {
        ungetc(ch, stdin);
        return 1;
    }
    return 0;
}

/* print the map */
void map_print(void)
{
    printf("\033[H\033[2J");
    int i;
    for (i = 0; i <= ROW - 1; i++)
        puts(map[i]);
}

/* initialize walls */
void init_walls(void)
{
    int wall_rows[NUM_OF_WALL] = {2, 4, 6, 10, 12, 14};
    
    for (int i = 0; i < NUM_OF_WALL; i++)
    {
        wall[i].x = wall_rows[i];
        wall[i].y = rand() % (COLUMN - WALL_LENGTH - 2) + 1;
        
        // draw walls on the map
        for (int j = 0; j < WALL_LENGTH; j++)
        {
            int pos = wall[i].y + j;
            if (pos >= 1 && pos < COLUMN - 1)
            {
                map[wall[i].x][pos] = WALL;
            }
        }
    }
}

/* wall movement logic */
void move_wall(int index, int direction)
{
    // clear current wall position
    for (int i = 0; i < WALL_LENGTH; i++)
    {
        int pos = wall[index].y + i;
        if (pos >= 1 && pos < COLUMN - 1)
        {
            if (map[wall[index].x][pos] == WALL)
            {
                map[wall[index].x][pos] = ' ';
            }
        }
    }
    
    // update wall position
    wall[index].y += direction;
    
    // wrap around handling - seamless wrapping
    if (wall[index].y < 1 - WALL_LENGTH + 1)
    {
        // moving left, wrap to right
        wall[index].y += (COLUMN - 2);
    }
    else if (wall[index].y >= COLUMN - 1)
    {
        // moving right, wrap to left
        wall[index].y -= (COLUMN - 2);
    }
    
    // draw new wall position with wrapping
    for (int i = 0; i < WALL_LENGTH; i++)
    {
        int pos = wall[index].y + i;
        
        // handle wrapping for each segment
        if (pos < 1)
        {
            pos += (COLUMN - 2);
        }
        else if (pos >= COLUMN - 1)
        {
            pos -= (COLUMN - 2);
        }
        
        // check collision with player
        if (wall[index].x == player_x && pos == player_y)
        {
            stop_game = 1;
            printf("\033[H\033[2J");
            printf("You lose the game!\n");
            return;
        }
        
        // don't overwrite player
        if (map[wall[index].x][pos] != PLAYER)
        {
            map[wall[index].x][pos] = WALL;
        }
    }
}

/* wall movement thread */
void *wall_move(void *arg)
{
    long index = (long)arg;
    int direction = (index % 2 == 0) ? 1 : -1;  // even: right, odd: left
    
    while (!stop_game)
    {
        pthread_mutex_lock(&mutex);
        move_wall(index, direction);
        pthread_mutex_unlock(&mutex);
        usleep(100000);  // 100ms delay
    }
    
    pthread_exit(NULL);
}

/* auto refresh screen thread */
void *auto_refresh(void *arg)
{
    while (!stop_game)
    {
        pthread_mutex_lock(&mutex);
        map_print();  // refresh the map display
        pthread_mutex_unlock(&mutex);
        usleep(50000);  // 50ms refresh rate (~20 FPS)
    }
    pthread_exit(NULL);
}

/* main function */
int main(int argc, char *argv[])
{
    srand(time(NULL));
    int i, j;

    /* initialize the map */
    memset(map, 0, sizeof(map));
    for (i = 1; i <= ROW - 2; i++)
    {
        for (j = 1; j <= COLUMN - 2; j++)
        {
            map[i][j] = ' ';
        }
    }
    for (j = 1; j <= COLUMN - 2; j++)
    {
        map[0][j] = HORI_LINE;
        map[ROW - 1][j] = HORI_LINE;
    }
    for (i = 1; i <= ROW - 2; i++)
    {
        map[i][0] = VERT_LINE;
        map[i][COLUMN - 1] = VERT_LINE;
    }
    map[0][0] = CORNER;
    map[0][COLUMN - 1] = CORNER;
    map[ROW - 1][0] = CORNER;
    map[ROW - 1][COLUMN - 1] = CORNER;

    player_x = ROW / 2;
    player_y = COLUMN / 2;
    map[player_x][player_y] = PLAYER;

    // initialize walls
    init_walls();

    // initialize mutex
    pthread_mutex_init(&mutex, NULL);

    // create wall threads
    pthread_t wall_threads[NUM_OF_WALL];
    for (long i = 0; i < NUM_OF_WALL; i++)
    {
        pthread_create(&wall_threads[i], NULL, wall_move, (void *)i);
    }

    // create auto refresh thread
    pthread_t refresh_thread;
    pthread_create(&refresh_thread, NULL, auto_refresh, NULL);

    // wait for game to end (for now, just wait a bit to see walls move)
    sleep(10);
    stop_game = 1;

    // wait for all wall threads to finish
    for (int i = 0; i < NUM_OF_WALL; i++)
    {
        pthread_join(wall_threads[i], NULL);
    }

    // wait for refresh thread
    pthread_join(refresh_thread, NULL);

    // cleanup
    pthread_mutex_destroy(&mutex);

    return 0;
}
