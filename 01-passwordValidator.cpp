#include <iostream>
#include <fstream>
#include <cstring>
#include <csignal>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <sys/wait.h>
#include <unistd.h>
#include <cctype>

using namespace std;

#define SHM_KEY 1234
#define SEM_KEY 5678
#define N_HIJOS 5

struct SharedData {
    char pwd[100];
    int flags[5];
    int score;
    bool nuevaClave;
    bool terminar;
};

int shmid, semid;
SharedData* shm_data;

union semun {
    int val;
};

void waitSemaphore() {
    struct sembuf sb = {0, -1, 0};
    semop(semid, &sb, 1);
}

void signalSemaphore() {
    struct sembuf sb = {0, 1, 0};
    semop(semid, &sb, 1);
}

void trabajoHijo(int tipo) {
    while (true) {
        usleep(50000);
        waitSemaphore();
        if (shm_data->terminar) {
            signalSemaphore();
            break;
        }
        if (!shm_data->nuevaClave) {
            signalSemaphore();
            continue;
        }

        switch (tipo) {
            case 0:
                shm_data->flags[0] = strlen(shm_data->pwd) >= 12 ? 1 : 0;
                break;
            case 1: {
                bool may = false, min = false;
                for (char c : string(shm_data->pwd)) {
                    if (isupper(c)) may = true;
                    if (islower(c)) min = true;
                }
                shm_data->flags[1] = (may && min) ? 1 : 0;
                break;
            }
            case 2: {
                bool num = false;
                for (char c : string(shm_data->pwd)) {
                    if (isdigit(c)) num = true;
                }
                shm_data->flags[2] = num ? 1 : 0;
                break;
            }
            case 3: {
                const char* symbols = "!@#$%^&*";
                shm_data->flags[3] = strpbrk(shm_data->pwd, symbols) ? 1 : 0;
                break;
            }
            case 4: {
                int score = 0;
                if (shm_data->flags[0]) score += 2;
                if (shm_data->flags[1]) score += 2;
                if (shm_data->flags[2]) score += 1;
                if (shm_data->flags[3]) score += 2;
                shm_data->score = score;
                break;
            }
        }
        signalSemaphore();
    }
    _exit(0);
}

void limpiarRecursos() {
    shmdt(shm_data);
    shmctl(shmid, IPC_RMID, nullptr);
    semctl(semid, 0, IPC_RMID);
}

int main() {
    signal(SIGINT, [](int){ limpiarRecursos(); exit(0); });

    shmid = shmget(SHM_KEY, sizeof(SharedData), IPC_CREAT | 0666);
    if (shmid < 0) { perror("shmget"); return 1; }
    shm_data = (SharedData*)shmat(shmid, nullptr, 0);

    semid = semget(SEM_KEY, 1, IPC_CREAT | 0666);
    semun arg; arg.val = 1;
    semctl(semid, 0, SETVAL, arg);

    shm_data->terminar = false;
    shm_data->nuevaClave = false;

    pid_t hijos[N_HIJOS];
    for (int i = 0; i < N_HIJOS; ++i) {
        if ((hijos[i] = fork()) == 0) {
            trabajoHijo(i);
        }
    }

    ifstream file("passwords.txt");
    string linea;
    while (getline(file, linea)) {
        waitSemaphore();
        strncpy(shm_data->pwd, linea.c_str(), sizeof(shm_data->pwd) - 1);
        shm_data->pwd[sizeof(shm_data->pwd) - 1] = '\0';
        memset(shm_data->flags, 0, sizeof(shm_data->flags));
        shm_data->score = 0;
        shm_data->nuevaClave = true;
        signalSemaphore();

        // Esperar a que el último proceso (puntaje) termine
        usleep(200000);

        waitSemaphore();
        shm_data->nuevaClave = false;
        cout << "\nContraseña: " << shm_data->pwd << endl;
        cout << "Longitud mínima: " << (shm_data->flags[0] ? "✔️" : "❌") << endl;
        cout << "Mayúsculas y minúsculas: " << (shm_data->flags[1] ? "✔️" : "❌") << endl;
        cout << "Contiene número: " << (shm_data->flags[2] ? "✔️" : "❌") << endl;
        cout << "Símbolo especial: " << (shm_data->flags[3] ? "✔️" : "❌") << endl;
        cout << "Puntaje total: " << shm_data->score << "/7\n";
        signalSemaphore();
    }

    file.close();
    waitSemaphore();
    shm_data->terminar = true;
    signalSemaphore();

    for (int i = 0; i < N_HIJOS; ++i) waitpid(hijos[i], nullptr, 0);
    limpiarRecursos();
    return 0;
}
