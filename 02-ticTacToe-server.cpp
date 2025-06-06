// Servidor de Tres en Raya con sockets TCP y threads en C++
#include <algorithm>
#include <iostream>
#include <string>
#include <cstring>
#include <thread>
#include <mutex>
#include <queue>
#include <vector>
#include <unordered_map>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <semaphore>

#define PORT 5000
#define MAX_CLIENTS 10

using namespace std;

mutex lobby_mutex;
queue<int> lobby;
std::counting_semaphore<MAX_CLIENTS> connection_slots(MAX_CLIENTS);
bool server_running = true;

struct Game {
    int client1, client2;
    char board[3][3];
    int turn; // 0 o 1
    mutex game_mutex;

    Game(int c1, int c2) : client1(c1), client2(c2), turn(0) {
        memset(board, ' ', sizeof(board));
    }

    void sendToBoth(const string& msg) {
        send(client1, msg.c_str(), msg.size(), 0);
        send(client2, msg.c_str(), msg.size(), 0);
    }
};

void close_client(int client_fd) {
    close(client_fd);
    connection_slots.release();
}

bool check_winner(char board[3][3], char sym) {
    for (int i = 0; i < 3; ++i) {
        if (board[i][0] == sym && board[i][1] == sym && board[i][2] == sym) return true; // fila
        if (board[0][i] == sym && board[1][i] == sym && board[2][i] == sym) return true; // columna
    }
    if (board[0][0] == sym && board[1][1] == sym && board[2][2] == sym) return true; // diagonal principal
    if (board[0][2] == sym && board[1][1] == sym && board[2][0] == sym) return true; // diagonal secundaria
    return false;
}

string format_board(const char board[3][3]) {
    string result;
    for (int i = 0; i < 3; ++i) {
        result += board[i][0];
        result += ' ';
        result += board[i][1];
        result += ' ';
        result += board[i][2];
        result += '\n';
    }
    return result;
}


void handle_game(Game* game) {
    int current_client = game->turn == 0 ? game->client1 : game->client2;
    int opponent = game->turn == 0 ? game->client2 : game->client1;
    
    game->sendToBoth("+START X\n");
    send(current_client, "+TURN\n", 6, 0);

    char buffer[1024];
    while (true) {
        memset(buffer, 0, sizeof(buffer));
        ssize_t len = recv(current_client, buffer, sizeof(buffer) - 1, 0);
        if (len <= 0) {
            send(opponent, "+RIVAL_QUIT\n", 13, 0);
            break;
        }

        string cmd(buffer);
        cmd.erase(std::remove(cmd.begin(), cmd.end(), '\r'), cmd.end());
        cmd.erase(std::remove(cmd.begin(), cmd.end(), '\n'), cmd.end());
        if (cmd.starts_with("MOVE")) {
            int r, c;
            if (sscanf(buffer, "MOVE %d %d", &r, &c) == 2 && r >= 0 && r < 3 && c >= 0 && c < 3) {
                lock_guard<mutex> lock(game->game_mutex);
                if (game->board[r][c] == ' ') {
                    game->board[r][c] = game->turn == 0 ? 'X' : 'O';
                    send(current_client, "+OK\n", 4, 0);

                    char sym = game->turn == 0 ? 'X' : 'O';
                    if (check_winner(game->board, sym)) {
                        send(current_client, "+WIN\n", 5, 0);
                        send(opponent, "+LOSE\n", 6, 0);
                        break;
                    }
                    
                    string move_msg = "MOVE " + to_string(r) + " " + to_string(c) + " " + string(1, sym) + "\n";
                    send(opponent, move_msg.c_str(), move_msg.size(), 0);

                    game->turn = 1 - game->turn;
                    swap(current_client, opponent);
                    send(current_client, "+TURN\n", 6, 0);

                } else {
                    send(current_client, "-ERR Invalid move\n", 19, 0);
                }
            } else {
                send(current_client, "-ERR Bad command\n", 18, 0);
            }
        } else if (cmd.starts_with("QUIT")) {
            send(current_client, "+BYE\n", 5, 0);
            send(opponent, "+RIVAL_QUIT\n", 13, 0);
            break;
        }else if (cmd.starts_with("BOARD")) {
            lock_guard<mutex> lock(game->game_mutex);
            string board_str = format_board(game->board);
            send(current_client, board_str.c_str(), board_str.size(), 0);
        }else {
            send(current_client, "-ERR Unknown\n", 13, 0);
        }
    }

    close_client(game->client1);
    close_client(game->client2);
    delete game;
}

void matchmaker() {
    while (server_running) {
        unique_lock<mutex> lock(lobby_mutex);
        if (lobby.size() >= 2) {
            int c1 = lobby.front(); lobby.pop();
            int c2 = lobby.front(); lobby.pop();
            lock.unlock();

            Game* game = new Game(c1, c2);
            thread game_thread(handle_game, game);
            game_thread.detach();
        } else {
            lock.unlock();
            this_thread::sleep_for(chrono::milliseconds(100));
        }
    }
}

void handle_client(int client_fd) {
    char buffer[1024];
    send(client_fd, "PLAY MOVE BOARD QUIT\n", 22, 0);

    while (true) {
        memset(buffer, 0, sizeof(buffer));
        ssize_t len = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
        if (len <= 0) break;

        string cmd(buffer);
        cmd.erase(remove(cmd.begin(), cmd.end(), '\r'), cmd.end());
       cmd.erase(remove(cmd.begin(), cmd.end(), '\n'), cmd.end());
        if (cmd.starts_with("PLAY")) {
            send(client_fd, "+WAIT\n", 6, 0);
            {
                lock_guard<mutex> lock(lobby_mutex);
                lobby.push(client_fd);
            }
            break;
        } else if (cmd.starts_with("HELP")) {
            send(client_fd, "PLAY MOVE BOARD QUIT\n", 22, 0);
        } else if (cmd.starts_with("QUIT")) {
            send(client_fd, "+BYE\n", 5, 0);
            break;
        } else {
            send(client_fd, "-ERR Unknown\n", 13, 0);
        }
    }

    // si sale sin entrar en lobby
    connection_slots.release();
}

int main() {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    bind(server_fd, (struct sockaddr*)&address, sizeof(address));
    listen(server_fd, 10);

    thread matchmaking_thread(matchmaker);

    while (server_running) {
    connection_slots.acquire();  // Limita la cantidad de conexiones activas

    sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);
    int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &addr_len);

    if (client_fd < 0) {
        perror("accept");
        connection_slots.release();  // Libera el slot si no se pudo aceptar
        continue;
    }

    thread client_thread(handle_client, client_fd);
    client_thread.detach();
}

    matchmaking_thread.join();
    close(server_fd);
    return 0;
}