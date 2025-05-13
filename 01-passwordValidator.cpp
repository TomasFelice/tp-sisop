#include <iostream>
#include <cstring>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <sys/wait.h>
#include <unistd.h>
#include <cctype>
#include <csignal>

using namespace std;

#define N_PROCESOS 5
#define SHM_KEY 1234
#define SEM_KEY 5678

struct SharedData {
    char pwd[100];
    int flags[5];
    int score;
};

int shmid, semid;
SharedData* data;

union semun {
    int val;
};

// Manejamos los semaforos de esta manera y no como se vio en clase porque esta forma nos permite monitorearlos con ipcs -s
void waitSemaphore() {
    struct sembuf sb = {0, -1, 0}; // P
    semop(semid, &sb, 1);
}

void signalSemaphore() {
    struct sembuf sb = {0, 1, 0}; // V
    semop(semid, &sb, 1);
}

void validarLongitud() {
    waitSemaphore();
    bool ok = strlen(data->pwd) >= 12;
    data->flags[0] = ok ? 1 : 0;
    signalSemaphore();
    _exit(0);
}

void validarMayusculasYMinusculas() {
    waitSemaphore();
    bool hasUpper = false, hasLower = false;
    for (char c : string(data->pwd)) {
        if (isupper(c)) hasUpper = true;
        if (islower(c)) hasLower = true;
    }
    data->flags[1] = (hasUpper && hasLower) ? 1 : 0;
    signalSemaphore();
    _exit(0);
}

void validarNumeros() {
    waitSemaphore();
    for (char c : string(data->pwd)) {
        if (isdigit(c)) {
            data->flags[2] = 1;
            signalSemaphore();
            _exit(0);
        }
    }
    data->flags[2] = 0;
    signalSemaphore();
    _exit(0);
}

void validarSimbolos() {
    waitSemaphore();
    const char* symbols = "!@#$%^&*";
    data->flags[3] = strpbrk(data->pwd, symbols) ? 1 : 0;
    signalSemaphore();
    _exit(0);
}

void hijo5() {
    while (true) {
        waitSemaphore();
        bool ready = true;
        for (int i = 0; i < 4; ++i) {
            if (data->flags[i] != 1) {
                ready = false;
                break;
            }
        }
        if (ready) {
            data->score = 0;
            if (data->flags[0]) data->score += 2;
            if (data->flags[1]) data->score += 2;
            if (data->flags[2]) data->score += 1;
            if (data->flags[3]) data->score += 2;
            signalSemaphore();
            break;
        }
        signalSemaphore();
        usleep(100000); // Evita consumo excesivo
    }
    _exit(0);
}

void limpiarRecursos() {
    shmdt(data);
    shmctl(shmid, IPC_RMID, nullptr);
    semctl(semid, 0, IPC_RMID);
}

int main() {
    signal(SIGINT, [](int){ limpiarRecursos(); exit(0); });

    // Crear memoria compartida
    shmid = shmget(SHM_KEY, sizeof(SharedData), IPC_CREAT | 0666);
    if (shmid < 0) {
        perror("shmget");
        return 1;
    }

    data = (SharedData*)shmat(shmid, nullptr, 0);

    // Crear semáforo
    semid = semget(SEM_KEY, 1, IPC_CREAT | 0666);
    semun arg;
    arg.val = 1;
    semctl(semid, 0, SETVAL, arg);

    // Leer contraseña
    cout << "Ingrese la contraseña a validar: ";
    cin.getline(data->pwd, 100);
    memset(data->flags, 0, sizeof(data->flags));
    data->score = 0;

    // Crear hijos
    pid_t hijos[N_PROCESOS];
    void (*funciones[N_PROCESOS])() = {validarLongitud, validarMayusculasYMinusculas, validarNumeros, validarSimbolos, hijo5};

    for (int i = 0; i < N_PROCESOS; ++i) {
        if ((hijos[i] = fork()) == 0) {
            funciones[i]();
        }
    }

    for (int i = 0; i < N_PROCESOS; ++i) waitpid(hijos[i], nullptr, 0);

    // Mostrar resultados
    cout << "\nResultados de validación:\n";
    cout << "Longitud mínima: " << (data->flags[0] ? "✔️" : "❌") << endl;
    cout << "Mayúsculas y minúsculas: " << (data->flags[1] ? "✔️" : "❌") << endl;
    cout << "Contiene número: " << (data->flags[2] ? "✔️" : "❌") << endl;
    cout << "Símbolo especial: " << (data->flags[3] ? "✔️" : "❌") << endl;
    cout << "Puntaje total: " << data->score << "/7\n";

    // Limpiar recursos
    limpiarRecursos();

    return 0;
}

// Compilar: g++ -o passwordValidator 01-passwordValidator.cpp -lpthread
// Ejecutar: ./passwordValidator
