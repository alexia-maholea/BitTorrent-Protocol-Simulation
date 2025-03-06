#include <mpi.h>
#include <pthread.h>
#include <iostream>
#include <cstdlib>
#include <string>
#include <vector>
#include <fstream>

#define TRACKER_RANK 0
#define MAX_FILES 10
#define MAX_FILENAME 15
#define HASH_SIZE 32
#define MAX_CHUNKS 100

#define INFO_TAG 1
#define CHUNK_TAG 2
#define FILE_TAG 3
#define RESPONSE_TAG 4
#define UPLOAD_TAG 5

using namespace std;

struct file {
    char name[MAX_FILENAME];
    int num_chunks;
    vector<string> chunks;
    vector<int> seeds;
    vector<int> peers;
};

struct file_request {
    char name[MAX_FILENAME];
    int chunks[MAX_CHUNKS];
    int num_chunks;
};

struct file_owned {
    char name[MAX_FILENAME];
    int num_chunks;
    char chunks[MAX_CHUNKS][HASH_SIZE + 1];
};

vector<file> files;

int num_requests;
file_request requests[MAX_FILES];

int num_owned;
file_owned files_owned[MAX_FILES];


void create_out(int rank, file_owned* file) {
    string filename = "client" + to_string(rank) + "_" + file->name;
    ofstream out(filename);

    //  we assemble the file
    for (int i = 0; i < file->num_chunks; i++) {
        out<<file->chunks[i];
        if (i != file->num_chunks - 1) {
            out<<endl;
        }
    }

    out.close();

}

void* download_thread_func(void* arg) {
    int rank = *static_cast<int*>(arg); 

    int downloads = 0;
    for (int i = 0; i < num_requests; i++) {
        printf("Peer %d is requesting file %s\n", rank, requests[i].name);
        char download_ack[10] = "DOWNLOAD";

        //  ask tracker for swarm list
        MPI_Send(&download_ack, 10, MPI_CHAR, TRACKER_RANK, INFO_TAG, MPI_COMM_WORLD);
        MPI_Send(&requests[i].name, MAX_FILENAME, MPI_CHAR, TRACKER_RANK, INFO_TAG, MPI_COMM_WORLD);


        //  receive swarm list
        vector<int> owners;
        int num_owners;
        MPI_Recv(&num_owners, 1, MPI_INT, TRACKER_RANK, RESPONSE_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        for (int j = 0; j < num_owners; j++) {
            int owner;
            MPI_Recv(&owner, 1, MPI_INT, TRACKER_RANK, RESPONSE_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            owners.push_back(owner);
        }

        //  number of chunks needed to complete the file
        MPI_Recv(&requests[i].num_chunks, 1, MPI_INT, TRACKER_RANK, RESPONSE_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

        //  add the file to our owned files
        files_owned[num_owned].num_chunks = 0;
        strcpy(files_owned[num_owned].name, requests[i].name);

        for (int j = 0; j < requests[i].num_chunks; j++) {
            // initialize chunks because we dont have any yet
            files_owned[num_owned].chunks[j][0] = '\0';
        }
        

        //  use round robin to download chunks
        int last_owner_index = 0;
        char ack[5];

        for (int j = 0; j < requests[i].num_chunks; j++) {
            if (requests[i].chunks[j] == -1) {
                // send request to download chunk to a peer
                do {
                    last_owner_index = (last_owner_index + 1) % owners.size();

                    char request[10] = "REQUEST";
                    MPI_Send(&request, 10, MPI_CHAR, owners[last_owner_index], UPLOAD_TAG, MPI_COMM_WORLD);
                    MPI_Send(&requests[i].name, MAX_FILENAME, MPI_CHAR, owners[last_owner_index], UPLOAD_TAG, MPI_COMM_WORLD);
                    MPI_Send(&j, 1, MPI_INT, owners[last_owner_index], UPLOAD_TAG, MPI_COMM_WORLD);

                    //  receive acknowledge
                    MPI_Recv(&ack, 5, MPI_CHAR, owners[last_owner_index], CHUNK_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

                } while (strcmp(ack, "ACK") != 0);
            
                //  add chunk to our file
                char chunk[HASH_SIZE + 1];
                MPI_Recv(&chunk, HASH_SIZE, MPI_CHAR, owners[last_owner_index], CHUNK_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                chunk[HASH_SIZE] = '\0';

                strcpy(files_owned[num_owned].chunks[j], chunk);

                //  mark chunk as downloaded
                requests[i].chunks[j] = 1;
                files_owned[num_owned].num_chunks++;

                downloads++;
            }

            //  after 10 downloads, we ask for an updated swarm list
            if (downloads == 10) {
                downloads = 0;

                //  request for updated swarm list
                char actualize[10] = "ACTUALIZE";
                MPI_Send(&actualize, 10, MPI_CHAR, TRACKER_RANK, INFO_TAG, MPI_COMM_WORLD);
                MPI_Send(&requests[i].name, MAX_FILENAME, MPI_CHAR, TRACKER_RANK, INFO_TAG, MPI_COMM_WORLD);

                owners.clear();
                MPI_Recv(&num_owners, 1, MPI_INT, TRACKER_RANK, RESPONSE_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                for (int j = 0; j < num_owners; j++) {
                    int owner;
                    MPI_Recv(&owner, 1, MPI_INT, TRACKER_RANK, RESPONSE_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                    owners.push_back(owner);
                }
            }
        }

        cout<<"Peer "<<rank<<" has completed downloading file "<<requests[i].name<<endl;
        create_out(rank, &files_owned[num_owned]);

        //  send done downloading file to tracker
        char done[10] = "FINISHED";
        MPI_Send(&done, 10, MPI_CHAR, TRACKER_RANK, INFO_TAG, MPI_COMM_WORLD);
        MPI_Send(&requests[i].name, MAX_FILENAME, MPI_CHAR, TRACKER_RANK, INFO_TAG, MPI_COMM_WORLD);

        num_owned++;
    }

    cout<<"Peer "<<rank<<" is done downloading"<<endl;
    char done[5] = "DONE";
    MPI_Send(&done, 5, MPI_CHAR, TRACKER_RANK, INFO_TAG, MPI_COMM_WORLD);

    return nullptr;
}

void* upload_thread_func(void* arg) {
    int rank = *static_cast<int*>(arg);

    while (1) {
        MPI_Status status;
        char request[10];
        MPI_Recv(&request, 10, MPI_CHAR, MPI_ANY_SOURCE, UPLOAD_TAG, MPI_COMM_WORLD, &status);

        //  someone wants to download a chunk
        if (strcmp(request, "REQUEST") == 0) {
            char file_name[MAX_FILENAME];
            MPI_Recv(&file_name, MAX_FILENAME, MPI_CHAR, status.MPI_SOURCE, UPLOAD_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

            int index;
            MPI_Recv(&index, 1, MPI_INT, status.MPI_SOURCE, UPLOAD_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE); 

            //  check if we have the file
            bool found = false;
            int i;
            for (i = 0; i < num_owned + 1; i++) {
                if (strcmp(files_owned[i].name, file_name) == 0) {
                    found = true;
                    break;
                }
            }

            if (!found) {
                //  we don't have the file
                char nack[5] = "NACK";
                MPI_Send(&nack, 5, MPI_CHAR, status.MPI_SOURCE, CHUNK_TAG, MPI_COMM_WORLD);
            } else {
                //  we have some part of the file, so we check if we have the chunk
                if (files_owned[i].chunks[index][0] == '\0') {
                    char nack[5] = "NACK";
                    MPI_Send(&nack, 5, MPI_CHAR, status.MPI_SOURCE, CHUNK_TAG, MPI_COMM_WORLD);
                } else {
                    char ack[5] = "ACK";
                    MPI_Send(&ack, 5, MPI_CHAR, status.MPI_SOURCE, CHUNK_TAG, MPI_COMM_WORLD);

                    //  send the chunk too
                    MPI_Send(&files_owned[i].chunks[index], HASH_SIZE, MPI_CHAR, status.MPI_SOURCE, CHUNK_TAG, MPI_COMM_WORLD);
                }
            }
        //  everyone finished downloading, so we can stop uploading
        } else if (strcmp(request, "DONE") == 0) {
            printf("Peer %d is closed\n", rank);
            return nullptr;
        }
    }
}

bool check_everyone(vector<bool> peers) {
    for (unsigned int i = 1; i < peers.size(); i++) {
        if (!peers[i]) {
            return false;
        }
    }

    cout<<"All peers are done"<<endl;
    return true;
}

void tracker(int numtasks, int rank) {

    vector<bool> peers(numtasks, false);

    for (int i = 1; i < numtasks; i++) {
        // get number of files
        int files_owned;
        MPI_Recv(&files_owned, 1, MPI_INT, i, FILE_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

        // get files
        for (int j = 0; j < files_owned; j++) {
            file f;
            MPI_Recv(&f.name, MAX_FILENAME, MPI_CHAR, i, FILE_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            MPI_Recv(&f.num_chunks, 1, MPI_INT, i, FILE_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

            //  get chunks
            for (int k = 0; k < f.num_chunks; k++) {
                char chunk[HASH_SIZE];
                MPI_Recv(&chunk, HASH_SIZE, MPI_CHAR, i, FILE_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                f.chunks.push_back(chunk);
            }

            //  add to files list
            f.seeds.push_back(i);

            //  check if file already exists
            bool found = false;
            unsigned int k;
            for (k = 0; k < files.size(); k++) {
                if (strcmp(files[k].name, f.name) == 0) {
                    found = true;
                    break;
                }
            }


            if (!found) {
                //  if it doesn't exist, we add it
                files.push_back(f);
            } else {
                // otherwise, just update seeds
                files[k].seeds.push_back(i);
            }
        }
    }

    //  send peers ack to start downloading
    cout<<"Peers are ready to download"<<endl;
    for (int i = 1; i < numtasks; i++) {
        char ack[5] = "ACK";
        MPI_Send(&ack, 5, MPI_CHAR, i, FILE_TAG, MPI_COMM_WORLD);
    }


    while (true) {
        MPI_Status status;
        char request[10];
        MPI_Recv(&request, 10, MPI_CHAR, MPI_ANY_SOURCE, INFO_TAG, MPI_COMM_WORLD, &status);

        if (strcmp(request, "DOWNLOAD") == 0) {
            char file_name[MAX_FILENAME];
            MPI_Recv(&file_name, MAX_FILENAME, MPI_CHAR, status.MPI_SOURCE, INFO_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

            cout<<"Tracker received download request for file "<<file_name<<" from peer "<<status.MPI_SOURCE<<endl;
            // send file swarms
            for (unsigned int i = 0; i < files.size(); i++) {
                if (strcmp(files[i].name, file_name) == 0) {
                    //  seeds and peers
                    int owners = files[i].seeds.size() + files[i].peers.size();
                    MPI_Send(&owners, 1, MPI_INT, status.MPI_SOURCE, RESPONSE_TAG, MPI_COMM_WORLD);

                    //  send seeds
                    for (unsigned int j = 0; j < files[i].seeds.size(); j++) {
                        MPI_Send(&files[i].seeds[j], 1, MPI_INT, status.MPI_SOURCE, RESPONSE_TAG, MPI_COMM_WORLD);
                    }

                    //  send peers
                    for (unsigned int j = 0; j < files[i].peers.size(); j++) {
                        MPI_Send(&files[i].peers[j], 1, MPI_INT, status.MPI_SOURCE, RESPONSE_TAG, MPI_COMM_WORLD);
                    }

                    //  send number of chunks
                    MPI_Send(&files[i].num_chunks, 1, MPI_INT, status.MPI_SOURCE, RESPONSE_TAG, MPI_COMM_WORLD);
                    files[i].peers.push_back(status.MPI_SOURCE);
                }
            }
        } else if (strcmp(request, "ACTUALIZE") == 0) {
            char file_name[MAX_FILENAME];
            MPI_Recv(&file_name, MAX_FILENAME, MPI_CHAR, status.MPI_SOURCE, INFO_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

            cout<<"Tracker received actualize request for file "<<file_name<<" from peer"<<status.MPI_SOURCE<<endl;

            // send updated file swarms
            for (unsigned int i = 0; i < files.size(); i++) {
                if (strcmp(files[i].name, file_name) == 0) {
                    //  seeds and peers
                    int owners = files[i].seeds.size() + files[i].peers.size();
                    MPI_Send(&owners, 1, MPI_INT, status.MPI_SOURCE, RESPONSE_TAG, MPI_COMM_WORLD);

                    //  send seeds
                    for (unsigned int j = 0; j < files[i].seeds.size(); j++) {
                        MPI_Send(&files[i].seeds[j], 1, MPI_INT, status.MPI_SOURCE, RESPONSE_TAG, MPI_COMM_WORLD);
                    }

                    //  send peers
                    for (unsigned int j = 0; j < files[i].peers.size(); j++) {
                        MPI_Send(&files[i].peers[j], 1, MPI_INT, status.MPI_SOURCE, RESPONSE_TAG, MPI_COMM_WORLD);
                    }
                }
            }
        } else if (strcmp(request, "FINISHED") == 0)  {
            //  receive finished downloading file
            char file_name[MAX_FILENAME];
            MPI_Recv(&file_name, MAX_FILENAME, MPI_CHAR, status.MPI_SOURCE, INFO_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

            cout<<"Tracker received finished downloading file "<<file_name<<" from peer "<<status.MPI_SOURCE<<endl;

            //  remove from peers list and add to seeds list
            for (unsigned int i = 0; i < files.size(); i++) {
                if (strcmp(files[i].name, file_name) == 0) {
                    for (unsigned int j = 0; j < files[i].peers.size(); j++) {
                        if (files[i].peers[j] == status.MPI_SOURCE) {
                            files[i].peers.erase(files[i].peers.begin() + j);
                            files[i].seeds.push_back(status.MPI_SOURCE);
                            break;
                        }
                    }
                }
            }
        } else if (strcmp(request, "DONE") == 0) {
            cout<<"Tracker received done from peer "<<status.MPI_SOURCE<<endl;
            peers[status.MPI_SOURCE] = true;
        }

        if (check_everyone(peers)) {
            //  send done to all peers
            for (int i = 1; i < numtasks; i++) {
                char done[5] = "DONE";
                MPI_Send(&done, 5, MPI_CHAR, i, UPLOAD_TAG, MPI_COMM_WORLD);
            }
            cout<<"Tracker is done"<<endl;
            return;
        }
    }

}


void peer(int numtasks, int rank) {
    string filename = "in" + to_string(rank) + ".txt";
    ifstream in(filename);

    in>>num_owned;
    MPI_Send(&num_owned, 1, MPI_INT, TRACKER_RANK, FILE_TAG, MPI_COMM_WORLD);

    for (int i = 0; i < num_owned; i++) {
        char file_name[MAX_FILENAME];
        int num_chunks;
        in>>file_name>>num_chunks;

        strcpy(files_owned[i].name, file_name);
        files_owned[i].num_chunks = num_chunks;

        //  send file data to tracker
        MPI_Send(file_name, MAX_FILENAME, MPI_CHAR, TRACKER_RANK, FILE_TAG, MPI_COMM_WORLD);
        MPI_Send(&num_chunks, 1, MPI_INT, TRACKER_RANK, FILE_TAG, MPI_COMM_WORLD);

        for (int j = 0; j < num_chunks; j++) {
            char chunk[HASH_SIZE + 1];
            in>>chunk;

            //  send hashes
            MPI_Send(chunk, HASH_SIZE, MPI_CHAR, TRACKER_RANK, FILE_TAG, MPI_COMM_WORLD);

            strcpy(files_owned[i].chunks[j], chunk);
            files_owned[i].chunks[j][HASH_SIZE] = '\0';
        }

    }

    //  files to be downloaded
    in>>num_requests;

    for (int i = 0; i < num_requests; i++) {
        in>>requests[i].name;
        requests[i].num_chunks = 0;

        for (int j = 0; j < MAX_CHUNKS; j++) {
            requests[i].chunks[j] = -1;
        }
    }

    in.close();

    //  wait for tracker to finish mapping
    char ack[5];
    MPI_Recv(&ack, 5, MPI_CHAR, TRACKER_RANK, FILE_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

    pthread_t download_thread;
    pthread_t upload_thread;
    void* status;
    int r;

    r = pthread_create(&download_thread, nullptr, download_thread_func, static_cast<void*>(&rank));
    if (r) {
        std::cerr << "Error creating download thread\n";
        std::exit(-1);
    }

    r = pthread_create(&upload_thread, nullptr, upload_thread_func, static_cast<void*>(&rank));
    if (r) {
        std::cerr << "Error creating upload thread\n";
        std::exit(-1);
    }

    r = pthread_join(download_thread, &status);
    if (r) {
        std::cerr << "Error waiting for download thread\n";
        std::exit(-1);
    }

    r = pthread_join(upload_thread, &status);
    if (r) {
        std::cerr << "Error waiting for upload thread\n";
        std::exit(-1);
    }
}

int main(int argc, char* argv[]) {
    int numtasks, rank;

    printf("tema mea\n");
    int provided;
    MPI_Init_thread(&argc, &argv, MPI_THREAD_MULTIPLE, &provided);
    if (provided < MPI_THREAD_MULTIPLE) {
        std::cerr << "MPI does not support multi-threading\n";
        std::exit(-1);
    }
    MPI_Comm_size(MPI_COMM_WORLD, &numtasks);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    if (rank == TRACKER_RANK) {
        tracker(numtasks, rank);
    } else {
        peer(numtasks, rank);
    }

    MPI_Finalize();
    return 0;
}
