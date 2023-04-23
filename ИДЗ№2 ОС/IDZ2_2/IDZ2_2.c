#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <semaphore.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <string.h>

volatile sig_atomic_t stop_flag = 0;

typedef struct {
    int energy;
    int alive;
    int resting;
    int in_battle;
    sem_t sem;
} Player;

Player *players;
int num_players;
int num_rested_players = 0;
int *rested_players;

void cleanup() {
    if (munmap(players, num_players * sizeof(Player)) == -1) {
        perror("munmap");
        exit(EXIT_FAILURE);
    }
    free(rested_players);
}

void sigint_handler(int signal) {
    cleanup();
    stop_flag = 1;
    printf("Программа прервана пользователем\n");
    exit(0);
}

void battle_process(int index) {
    if (players[index].resting) {
        return;
    }
    // Генерируем случайное время боя.
    sleep(rand() % 3 + 1);

    sem_wait(&players[index].sem);
    if (players[index].in_battle == 0 && players[index].alive == 1) {
        players[index].in_battle = 1;
        int opponent_index = -1;
        for (int i = 0; i < num_players; i++) {
            if (i != index && players[i].in_battle == 0 && players[i].alive == 1) {
                opponent_index = i;
                break;
            }
        }

        if (opponent_index != -1) {
            // Запускаем сражение.
            players[opponent_index].in_battle = 1;
            printf("Игрок %d соревнуется с игроком %d\n", index, opponent_index);
            if (players[index].energy > players[opponent_index].energy) {
                players[index].energy += players[opponent_index].energy;
                players[opponent_index].alive = 0;
                printf("Игрок %d победил и получил энергию игрока %d, теперь его энергия равна %d\n", index,
                       opponent_index, players[index].energy);
            } else {
                players[opponent_index].energy += players[index].energy;
                players[index].alive = 0;
                printf("Игрок %d победил и получил энергию игрока %d, теперь его энергия равна %d\n", opponent_index,
                       index, players[opponent_index].energy);
            }
        } else {
            // Если игрок отдыхает, то удваивает энергию.
            players[index].energy *= 2;
            players[index].resting = 1;
            rested_players[num_rested_players++] = index;
            printf("Игрок %d удваивает свою энергию и отдыхает, теперь его энергия равна %d\n", index,
                   players[index].energy);
        }
    }
    sem_post(&players[index].sem);

    exit(0);
}

int main() {
    signal(SIGINT, sigint_handler);
    srand(time(NULL));
    char str[100];
    printf("Введите количество игроков или команду выхода: ");
    fgets(str, sizeof(str), stdin);
    if (strcmp(str, "q\n") == 0) {
        printf("Программа прервана пользователем\n");
        return 0;
    } else {
        num_players = atoi(str);
    }

    players = mmap(NULL, num_players * sizeof(Player), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (players == MAP_FAILED) {
        perror("mmap");
        exit(EXIT_FAILURE);
    }

    for (int i = 0; i < num_players; i++) {
        players[i].energy = rand() % 100 + 1;
        players[i].alive = 1;
        players[i].resting = 0;
        players[i].in_battle = 0;
        sem_init(&players[i].sem, 1, 1);
    }

    int alive_players = num_players;
    rested_players = malloc(num_players * sizeof(int));
    pid_t pids[num_players];

    while (alive_players > 1 && !stop_flag) {
        for (int i = 0; i < num_rested_players; i++) {
            players[rested_players[i]].resting = 0;
        }
        num_rested_players = 0;

        // Родительский процесс создает дочерние процессы для каждого активного игрока
        for (int i = 0; i < num_players; i++) {
            if (players[i].alive && !players[i].resting) {
                pids[i] = fork();
                if (pids[i] < 0) {
                    perror("fork");
                    exit(EXIT_FAILURE);
                } else if (pids[i] == 0) {
                    printf("Родительский процесс создал дочерний процесс для игрока %d\n", i);
                    battle_process(i);
                }
            }
        }

        // Родительский процесс ожидает завершения дочерних процессов
        for (int i = 0; i < num_players; i++) {
            if (players[i].alive && !players[i].resting) {
                waitpid(pids[i], NULL, 0);
                printf("Дочерний процесс для игрока %d завершился\n", i);
            }
        }

        // Сбрасываем флаги и подсчитываем выживших игроков.
        alive_players = 0;
        for (int i = 0; i < num_players; i++) {
            players[i].in_battle = 0;
            players[i].resting = 0;
            if (players[i].alive) {
                alive_players++;
            }
        }
    }
    // Выводим победителя.
    for (int i = 0; i < num_players; i++) {
        if (players[i].alive) {
            printf("Игрок %d победил в соревновании с энергией %d\n", i, players[i].energy);
            break;
        }
    }
    
    cleanup();

    return 0;
}
