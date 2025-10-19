// Code by LaNasil 123090669@link.cuhk.edu.cn
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <termios.h>
#include <fcntl.h>
#include <sys/select.h>

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
int gold_collected = 0; // number of gold pieces collected

struct termios orig_termios;  // save original terminal settings

// Position structure
struct Position {
    int x, y;
};

struct Position wall[NUM_OF_WALL];  // wall positions
struct Position gold[NUM_OF_GOLD];  // gold positions

/* functions */
int kbhit(void);
void map_print(void);
void init_walls(void);
void move_wall(int index, int direction);
void *wall_move(void *arg);
void *auto_refresh(void *arg);
void *player_move(void *arg);
void init_gold(void);
void move_gold_logic(int index);
void *gold_move(void *arg);
void enable_raw_mode(void);
void disable_raw_mode(void);

/* Enable raw mode (disable echo and canonical mode) */
void enable_raw_mode(void)
{
    struct termios raw;
    
    tcgetattr(STDIN_FILENO, &orig_termios);  // save original settings
    raw = orig_termios;
    
    raw.c_lflag &= ~(ICANON | ECHO);  // disable canonical mode and echo
    raw.c_cc[VMIN] = 0;   // non-blocking read
    raw.c_cc[VTIME] = 0;  // no timeout
    
    tcsetattr(STDIN_FILENO, TCSANOW, &raw);
    
    // set non-blocking mode
    int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);
}

/* Disable raw mode (restore original terminal settings) */
void disable_raw_mode(void)
{
    tcsetattr(STDIN_FILENO, TCSANOW, &orig_termios);
    
    // restore blocking mode
    int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    fcntl(STDIN_FILENO, F_SETFL, flags & ~O_NONBLOCK);
}

/* Determine a keyboard is hit or not.
 * If yes, return 1. If not, return 0. */

// NOTE that the  original template function kbhit() will ocationally change terminal settings every time it is called. 
// Thus casuing flickering letters on the screen. The following implementation uses select() to check keyboard input without changing terminal settings.

int kbhit(void)
{
    struct timeval tv;
    fd_set fds;
    tv.tv_sec = 0;
    tv.tv_usec = 0;
    FD_ZERO(&fds);
    FD_SET(STDIN_FILENO, &fds);
    select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv);
    return FD_ISSET(STDIN_FILENO, &fds);
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

/* initialize gold */
void init_gold(void)
{
    int gold_rows[NUM_OF_GOLD] = {1, 3, 5, 11, 13, 15};
    
    for (int i = 0; i < NUM_OF_GOLD; i++)
    {
        gold[i].x = gold_rows[i];
        gold[i].y = rand() % (COLUMN - 2) + 1;
        
        // draw gold on the map
        map[gold[i].x][gold[i].y] = GOLD;
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
            printf("You lose the game!!\n");
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

/* player movement thread */
void *player_move(void *arg)
{
    while (!stop_game)
    {
        if (kbhit())
        {
            char dir = getchar();
            
            pthread_mutex_lock(&mutex);
            
            // clear old player position
            map[player_x][player_y] = ' ';
            
            // check if 'q' is pressed to exit
            if (dir == 'q' || dir == 'Q')
            {
                stop_game = 1;
                printf("\033[H\033[2J");
                printf("You exit the game.\n");
                pthread_mutex_unlock(&mutex);
                break;
            }
            
            // update position based on WASD
            if ((dir == 'w' || dir == 'W') && player_x > 1)
                player_x--;
            if ((dir == 's' || dir == 'S') && player_x < ROW - 2)
                player_x++;
            if ((dir == 'a' || dir == 'A') && player_y > 1)
                player_y--;
            if ((dir == 'd' || dir == 'D') && player_y < COLUMN - 2)
                player_y++;
            
            // collision detection with walls
            if (map[player_x][player_y] == WALL)
            {
                map[player_x][player_y] = PLAYER;  // show player embedded in wall
                stop_game = 1;
                printf("\033[H\033[2J");
                printf("You lose the game!!\n");
                pthread_mutex_unlock(&mutex);
                break;
            }
            
            // check if player collected gold
            for (int i = 0; i < NUM_OF_GOLD; i++)
            {
                if (player_x == gold[i].x && player_y == gold[i].y)
                {
                    gold_collected++;
                    gold[i].x = -1;  // mark gold as collected
                    gold[i].y = -1;
                    
                    // check if all gold collected
                    if (gold_collected == NUM_OF_GOLD)
                    {
                        stop_game = 1;
                        printf("\033[H\033[2J");
                        printf("You win the game!!\n");
                        pthread_mutex_unlock(&mutex);
                        break;
                    }
                }
            }
            
            if (stop_game)
            {
                break;
            }
            
            // update player's new position
            map[player_x][player_y] = PLAYER;
            
            pthread_mutex_unlock(&mutex);
        }
        
        usleep(10000);  // 10ms delay to prevent high CPU usage
    }
    
    pthread_exit(NULL);
}

/* gold movement logic */
void move_gold_logic(int index)
{
    static int direction[NUM_OF_GOLD] = {0};
    
    // initialize random direction for each gold
    if (direction[index] == 0)
    {
        direction[index] = (rand() % 2 == 0) ? 1 : -1;
    }
    
    // if gold is already collected, skip
    if (gold[index].x == -1)
        return;
    
    // clear current gold position
    if (map[gold[index].x][gold[index].y] == GOLD)
    {
        map[gold[index].x][gold[index].y] = ' ';
    }
    
    // update gold position
    gold[index].y += direction[index];
    
    // wrap around handling
    if (gold[index].y < 1)
    {
        gold[index].y = COLUMN - 2;
    }
    else if (gold[index].y >= COLUMN - 1)
    {
        gold[index].y = 1;
    }
    
    // check if gold moves to player's position
    if (gold[index].x == player_x && gold[index].y == player_y)
    {
        gold_collected++;
        gold[index].x = -1;  // mark as collected
        gold[index].y = -1;
        
        // check if all gold collected
        if (gold_collected == NUM_OF_GOLD)
        {
            stop_game = 1;
            printf("\033[H\033[2J");
            printf("You win the game!!\n");
            return;
        }
    }
    else
    {
        // draw gold at new position
        map[gold[index].x][gold[index].y] = GOLD;
    }
}

/* gold movement thread */
void *gold_move(void *arg)
{
    long index = (long)arg;
    
    while (!stop_game)
    {
        pthread_mutex_lock(&mutex);
        move_gold_logic(index);
        pthread_mutex_unlock(&mutex);
        usleep(100000);  // 100ms delay
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
    
    // initialize gold
    init_gold();

    // enable raw mode (disable echo, enable non-blocking input)
    enable_raw_mode();

    // initialize mutex
    pthread_mutex_init(&mutex, NULL);

    // create player movement thread
    pthread_t player_thread;
    pthread_create(&player_thread, NULL, player_move, NULL);

    // create wall threads
    pthread_t wall_threads[NUM_OF_WALL];
    for (long i = 0; i < NUM_OF_WALL; i++)
    {
        pthread_create(&wall_threads[i], NULL, wall_move, (void *)i);
    }
    
    // create gold threads
    pthread_t gold_threads[NUM_OF_GOLD];
    for (long i = 0; i < NUM_OF_GOLD; i++)
    {
        pthread_create(&gold_threads[i], NULL, gold_move, (void *)i);
    }

    // create auto refresh thread
    pthread_t refresh_thread;
    pthread_create(&refresh_thread, NULL, auto_refresh, NULL);

    // wait for player thread to finish (game ends when player quits or loses)
    pthread_join(player_thread, NULL);

    // wait for all wall threads to finish
    for (int i = 0; i < NUM_OF_WALL; i++)
    {
        pthread_join(wall_threads[i], NULL);
    }
    
    // wait for all gold threads to finish
    for (int i = 0; i < NUM_OF_GOLD; i++)
    {
        pthread_join(gold_threads[i], NULL);
    }

    // wait for refresh thread
    pthread_join(refresh_thread, NULL);

    // cleanup
    pthread_mutex_destroy(&mutex);
    
    // restore terminal settings
    disable_raw_mode();

    return 0;
}
