#include <unistd.h>
#include <pwd.h>
#include <iostream>
#include <vector>
#include <string>
#include <sstream>
#include <sys/types.h>
#include <stdio.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <cstdlib>
#include <termios.h>
#include <sstream>
using namespace std;

enum state { command_state_01, command_state_02, parameter_state_01, parameter_state_02, intermediate_state };

string homeDir;
bool Exit_detection = false;
int Pipe_Amount;

struct command {
	string string_command;
	vector <string> string_inputs;
	pid_t pid;
};

/*=====================================================================================================*/
typedef struct job {
	vector <command> command_vector;
	pid_t pgid;
	bool Running;
	bool Completed;
	bool background_process; //The job is a background process
	string input_command; //Original input command string
}job;

pid_t shell_pgid;
struct termios shell_tmodes;
int shell_terminal;
int shell_is_interactive;

/*========================================================================================================*/


bool command_parser(string input_string, std::vector<command>& command_vector);
void command_parser_parser(vector<command>& command_vector);
bool Exec_Commands(vector<command>& command_vector);
void prompt_disp(struct passwd *pw);
void strcpy(string* c1, char* c2);
void strcpy(char* c1, string c2);
void strcpy(char *c1, const char* c2);

/*=============================Job control===================================================================*/

bool notification = false;

void init_job(vector <job>& job_vecotr, vector<command>& command_vector, string input_string);
void job_Exec(vector <job>& job_vector);
bool Exec_wo_waiting(vector<command>& command_vector);
bool job_check(vector <job>& job_vector);
void job_disp(vector <job>& job_vector);

bool bgp_found = false;
bool stop_signal_invoked = false;

/*===========================================================================================================*/

vector <job> job_vector;

void Done_handler(int sig);
void Stop_handler(int sig);

int main() {
	struct passwd *pw = getpwuid(getuid());
	homeDir = pw->pw_dir;
	while (!Exit_detection) {
		signal(SIGTSTP, SIG_IGN);
		signal(SIGTSTP, Stop_handler);
		signal(SIGCHLD, Done_handler);
		prompt_disp(pw);
		vector <command> commands;
		string input_string;
		getline(cin, input_string);
		if (input_string == "") continue;
		init_job(job_vector, commands, input_string);
		job_Exec(job_vector);
	}
	return 0;
}

bool command_parser(string input_string, std::vector<command>& command_vector) {
	command tmp;
	string tmp_string_parameter;
	tmp.string_command = tmp_string_parameter = "";
	state t = command_state_01;
	for (int i = 0; i < input_string.size(); i++) {
		switch (t) {
		case command_state_01:
			if (input_string[i] == ' ') continue;
			else if (input_string[i] == '|') return false;
			else {
				tmp.string_command += input_string[i];
				t = command_state_02;
			}
			break;
		case command_state_02:
			if (input_string[i] == ' ') {
				t = intermediate_state;
			}
			else if (input_string[i] == '|') {
				t = command_state_01;
				tmp.string_inputs.push_back(tmp_string_parameter);
				command_vector.push_back(tmp);
				tmp.string_command = "";
			}
			else {
				tmp.string_command += input_string[i];
			}
			break;
		case parameter_state_01:
			if (input_string[i] == ' ') continue;
			else {
				tmp_string_parameter += input_string[i];
				t = parameter_state_02;
			}
			break;
		case parameter_state_02:
			if (input_string[i] == '|') {
				if (tmp_string_parameter != "" && tmp_string_parameter != " ") {
					tmp.string_inputs.push_back(tmp_string_parameter);
				}
				command_vector.push_back(tmp);
				t = command_state_01;
				tmp.string_command = tmp_string_parameter = "";
				tmp.string_inputs.clear();
			}
			else if (input_string[i] == ' ') {
				tmp.string_inputs.push_back(tmp_string_parameter);
				tmp_string_parameter = "";
			}
			else {
				tmp_string_parameter += input_string[i];
			}
			break;
		case intermediate_state:
			if (input_string[i] == ' ') continue;
			else if (input_string[i] == '|') {
				t = command_state_01;
				command_vector.push_back(tmp);
				tmp.string_command = "";
			}
			else {
				tmp_string_parameter += input_string[i];
				t = parameter_state_02;
			}
			break;
		}
	}
	if (t == parameter_state_02 && (tmp_string_parameter != " " && tmp_string_parameter != "")) {
		tmp.string_inputs.push_back(tmp_string_parameter);
		command_vector.push_back(tmp);
	}
	else if (t == command_state_02) {
		command_vector.push_back(tmp);
	}
	if (command_vector.back().string_inputs.size() > 0 && command_vector.back().string_inputs.back() == "&") {
		command_vector.back().string_inputs.pop_back();
		return true;
	}
	return false;
}

void command_parser_parser(vector<command>& command_vector) {
	//cout << command_vector.size() << endl;
	for (unsigned int i = 0; i < command_vector.size(); i++) {
		cout << "Command: " << command_vector.at(i).string_command << endl;
		cout << "Parameters: " << endl;
		for (unsigned int k = 0; k < command_vector.at(i).string_inputs.size(); k++) {
			cout << k << ":" << command_vector.at(i).string_inputs.at(k) << endl;
		}
		cout << command_vector.at(i).string_inputs.size() << endl;
	}
}

void prompt_disp(struct passwd *pw) {
	char currentDir[1024];
	getcwd(currentDir, sizeof(currentDir));
	cout << pw->pw_name << "@" << currentDir << ":";
}

void strcpy(string* c1, char* c2) {
	for (unsigned int i = 0; i < c1->size(); i++) c1[i] = c2[i];
}

void strcpy(char* c1, string c2) {
	const char* tmp = c2.c_str();
	unsigned int i;
	for (i = 0; i < c2.length(); i++) c1[i] = tmp[i];
	c1[i] = '\0';
}

void strcpy(char *c1, const char* c2) {
	int i;
	for (i = 0; c2[i] != '\0'; i++) {
		c1[i] = c2[i];
	}
	c1[i] = '\0';
}

bool Exec_Commands(vector<command>& command_vector) {
	int pipes[2];
	pipe(pipes);
	pid_t pid;
	int status;
	if (command_vector.size() == 1) {
		command_vector.at(0).pid = pid = fork();
		if (pid == 0) {
			if (command_vector.at(0).string_command == "cd") {
				if (command_vector.at(0).string_inputs.size() == 0) {
					char hmDir[1024];
					strcpy(hmDir, homeDir); //conversion of string to char
					chdir(hmDir);
				}
				else {
					char destDir[1024];
					getcwd(destDir, sizeof(destDir)); //get current directory
					string tmpDir = destDir;
					tmpDir += "/";
					tmpDir += command_vector.at(0).string_inputs.at(0); //Concatenate current directory with destination directory
					strcpy(destDir, tmpDir); //conversion of string to char	
					if (chdir(destDir)) {
						cout << "No such directory!" << endl;
					}
				}
			}
			else if (command_vector.at(0).string_command == "exit") {
				Exit_detection = true;
				return true;
			}
			else {
				if (command_vector.at(0).string_inputs.size() > 0) {
					char *argv[command_vector.at(0).string_inputs.size() + 2];
					argv[0] = new char[command_vector.at(0).string_command.length()];
					strcpy(argv[0], command_vector.at(0).string_command.c_str());
					unsigned int i1;
					//format of multi-arguments: { Command_name, argument_0, argument_1,..., argument_n, NULL}
					for (i1 = 0; i1 < command_vector.at(0).string_inputs.size(); i1++) {
						argv[i1 + 1] = new char[command_vector.at(0).string_inputs.at(i1).length()];
						strcpy(argv[i1 + 1], command_vector.at(0).string_inputs.at(i1).c_str());
					}
					argv[i1 + 1] = NULL;
					// For instance: char* argv[] = { "echo", "Hello", "world!", "pikachu", NULL };
					execvp(argv[0], argv);
				}
				else {
					execlp(command_vector.at(0).string_command.c_str(), command_vector.at(0).string_command.c_str(), NULL);
				}
			}
		}
	}
	else {
		for (unsigned int i = 0; i < command_vector.size(); i++) {
			pid = fork();
			if (pid == 0) {
				if (command_vector.at(i).string_command == "cd") {
					if (command_vector.at(i).string_inputs.size() == 0) {
						char hmDir[1024];
						strcpy(hmDir, homeDir); //conversion of string to char
						chdir(hmDir);
					}
					else {
						char destDir[1024];
						getcwd(destDir, sizeof(destDir)); //get current directory
						string tmpDir = destDir;
						tmpDir += "/";
						tmpDir += command_vector.at(i).string_inputs.at(0); //Concatenate current directory with destination directory
						strcpy(destDir, tmpDir); //conversion of string to char	
						if (chdir(destDir)) {
							cout << "No such directory!" << endl;
						}
					}
				}
				else if (command_vector.at(i).string_command == "exit") {
					Exit_detection = true;
					return true;
				}
				else {
					/*0 for reading 1 for writing*/
					if (i == 0) { //Leading process
						close(STDOUT_FILENO);
						close(pipes[0]); //Close read end of pipe in queue
						dup2(pipes[1], STDOUT_FILENO);
					}
					else if (i == command_vector.size() - 1) { //Last process in queue
						close(STDIN_FILENO);
						close(pipes[1]);
						dup2(pipes[0], STDIN_FILENO);
					}
					else { //Processes in middle of queue
						close(STDOUT_FILENO);
						close(STDIN_FILENO);
						dup2(pipes[0], STDIN_FILENO); //Connect input FD to read end of pipe from previous process
						dup2(pipes[1], STDOUT_FILENO); //Connect output FD to write end of pipe for next process
					}
					if (command_vector.at(i).string_inputs.size() == 0) {
						//execlp("wc", "wc", NULL);
						execlp(command_vector.at(i).string_command.c_str(), command_vector.at(i).string_command.c_str(), NULL);
					}
					else {
						char *argv[command_vector.at(i).string_inputs.size() + 2];
						argv[0] = new char[command_vector.at(i).string_command.length()];
						strcpy(argv[0], command_vector.at(i).string_command.c_str());
						unsigned int i1;
						//format of multi-arguments: { Command_name, argument_0, argument_1,..., argument_n, NULL}
						for (i1 = 0; i1 < command_vector.at(i).string_inputs.size(); i1++) {
							argv[i1 + 1] = new char[command_vector.at(i).string_inputs.at(i1).length()];
							strcpy(argv[i1 + 1], command_vector.at(i).string_inputs.at(i1).c_str());
						}
						argv[i1 + 1] = NULL;
						// For instance: char* argv[] = { "echo", "Hello", "world!", "pikachu", NULL };
						execvp(argv[0], argv);
					}
				}
			}
		}
	}
	if (pid > 0) {
		waitpid(-1, 0, WUNTRACED);
		if (kill(pid, 0) == 0) return false; //If child process still exist, meaning the process has not termianted yet
		command_vector.at(0).pid = pid;
	}
	return true; //Process executed successfully
}

void init_job(vector <job>& job_vector, vector<command>& command_vector, string input_string) {
	job new_job;
	if (command_parser(input_string, command_vector)) {
		new_job.background_process = true;
	}
	else new_job.background_process = false;
	new_job.command_vector = command_vector;
	new_job.Completed = false;
	new_job.Running = true;
	new_job.input_command = input_string;
	job_vector.push_back(new_job);
}

bool Exec_wo_waiting(vector<command>& command_vector) {
	int pipes[2];
	pipe(pipes);
	pid_t pid;
	int status;
	if (command_vector.size() == 1) {
		command_vector.at(0).pid = pid = fork();
		if (pid != 0) {
			cout << command_vector.at(0).pid << endl;
		}
		if (pid == 0) {
			if (command_vector.at(0).string_command == "cd") {
				if (command_vector.at(0).string_inputs.size() == 0) {
					char hmDir[1024];
					strcpy(hmDir, homeDir); //conversion of string to char
					chdir(hmDir);
				}
				else {
					char destDir[1024];
					getcwd(destDir, sizeof(destDir)); //get current directory
					string tmpDir = destDir;
					tmpDir += "/";
					tmpDir += command_vector.at(0).string_inputs.at(0); //Concatenate current directory with destination directory
					strcpy(destDir, tmpDir); //conversion of string to char	
					if (chdir(destDir)) {
						cout << "No such directory!" << endl;
					}
				}
			}
			else if (command_vector.at(0).string_command == "exit") {
				Exit_detection = true;
				return true;
			}
			else {
				if (command_vector.at(0).string_inputs.size() > 0) {
					char *argv[command_vector.at(0).string_inputs.size() + 2];
					argv[0] = new char[command_vector.at(0).string_command.length()];
					strcpy(argv[0], command_vector.at(0).string_command.c_str());
					unsigned int i1;
					//format of multi-arguments: { Command_name, argument_0, argument_1,..., argument_n, NULL}
					for (i1 = 0; i1 < command_vector.at(0).string_inputs.size(); i1++) {
						argv[i1 + 1] = new char[command_vector.at(0).string_inputs.at(i1).length()];
						strcpy(argv[i1 + 1], command_vector.at(0).string_inputs.at(i1).c_str());
					}
					argv[i1 + 1] = NULL;
					// For instance: char* argv[] = { "echo", "Hello", "world!", "pikachu", NULL };
					execvp(argv[0], argv);
				}
				else {
					execlp(command_vector.at(0).string_command.c_str(), command_vector.at(0).string_command.c_str(), NULL);
				}
			}
		}
	}
	else {
		for (unsigned int i = 0; i < command_vector.size(); i++) {
			command_vector.at(i).pid = pid = fork();
			//cout << command_vector.at(0).pid << endl;
			if (pid == 0) {
				if (command_vector.at(i).string_command == "cd") {
					if (command_vector.at(i).string_inputs.size() == 0) {
						char hmDir[1024];
						strcpy(hmDir, homeDir); //conversion of string to char
						chdir(hmDir);
					}
					else {
						char destDir[1024];
						getcwd(destDir, sizeof(destDir)); //get current directory
						string tmpDir = destDir;
						tmpDir += "/";
						tmpDir += command_vector.at(i).string_inputs.at(0); //Concatenate current directory with destination directory
						strcpy(destDir, tmpDir); //conversion of string to char	
						if (chdir(destDir)) {
							cout << "No such directory!" << endl;
						}
					}
				}
				else if (command_vector.at(i).string_command == "exit") {
					Exit_detection = true;
					return true;
				}
				else {
					/*0 for reading 1 for writing*/
					if (i == 0) { //Leading process
						close(STDOUT_FILENO);
						close(pipes[0]); //Close read end of pipe in queue
						dup2(pipes[1], STDOUT_FILENO);
					}
					else if (i == command_vector.size() - 1) { //Last process in queue
						close(STDIN_FILENO);
						close(pipes[1]);
						dup2(pipes[0], STDIN_FILENO);
					}
					else { //Processes in middle of queue
						close(STDOUT_FILENO);
						close(STDIN_FILENO);
						dup2(pipes[0], STDIN_FILENO); //Connect input FD to read end of pipe from previous process
						dup2(pipes[1], STDOUT_FILENO); //Connect output FD to write end of pipe for next process
					}
					if (command_vector.at(i).string_inputs.size() == 0) {
						execlp(command_vector.at(i).string_command.c_str(), command_vector.at(i).string_command.c_str(), NULL);
					}
					else {
						char *argv[command_vector.at(i).string_inputs.size() + 2];
						argv[0] = new char[command_vector.at(i).string_command.length()];
						strcpy(argv[0], command_vector.at(i).string_command.c_str());
						unsigned int i1;
						//format of multi-arguments: { Command_name, argument_0, argument_1,..., argument_n, NULL}
						for (i1 = 0; i1 < command_vector.at(i).string_inputs.size(); i1++) {
							argv[i1 + 1] = new char[command_vector.at(i).string_inputs.at(i1).length()];
							strcpy(argv[i1 + 1], command_vector.at(i).string_inputs.at(i1).c_str());
						}
						argv[i1 + 1] = NULL;
						// For instance: char* argv[] = { "echo", "Hello", "world!", "pikachu", NULL };
						execvp(argv[0], argv);
					}
				}
			}
		}
	}
	return true;
}

/*==============================================Handlers==================================================*/

void Done_handler(int sig) {
	pid_t return_pid = waitpid(-1, 0, WNOHANG);
	if (return_pid == 0) return;
	for (int i = 0; i < job_vector.size(); i++) {
		if (job_vector.at(i).command_vector.at(0).pid == job_vector.at(i).command_vector.at(0).pid) {
			job_vector.at(i).Completed = true;
			job_vector.at(i).Running = false;
		}
	}
	cout << endl;
	job_disp(job_vector);
}

void Stop_handler(int sig) {
	stop_signal_invoked = true;
	for (int i = 0; i < job_vector.size(); i++) {
		kill(job_vector.at(i).command_vector.at(0).pid, SIGSTOP);
		job_vector.at(i).Running = job_vector.at(i).Completed = false;
	}
	job_disp(job_vector);
}

/*===========================================================================================================*/

void job_disp(vector <job>& job_vector) {
	for (unsigned int i = 0; i < job_vector.size(); i++) {
		cout << "[" << i + 1 << "] ";
		if (job_vector.at(i).Completed == true) {
			cout << "Completed\t" << job_vector.at(i).input_command << endl;
			job_vector.erase(job_vector.begin() + i);
			continue;
		}
		else if (!job_vector.at(i).Completed && job_vector.at(i).Running) {
			cout << "Running\t" << job_vector.at(i).input_command << endl;
		}
		else if (!job_vector.at(i).Completed && !job_vector.at(i).Running) {
			cout << "Stopped\t" << job_vector.at(i).input_command << endl;
		}
	}
}

void job_Exec(vector <job>& job_vector) {
	if (job_vector.back().command_vector.back().string_command == "exit") {
			Exit_detection = true;
			return;
	}
	if (job_vector.back().background_process) {
		bgp_found = true;
		cout << "[" << job_vector.size() << "] ";
		Exec_wo_waiting(job_vector.back().command_vector);
	}
	else if (job_vector.back().command_vector.back().string_command == "jobs") {
		job_vector.erase(job_vector.begin() + job_vector.size() - 1);
		job_disp(job_vector);
	}
	else if (job_vector.back().command_vector.back().string_command == "fg") {
		string dest_job_str = job_vector.back().command_vector.back().string_inputs.at(0);
		stringstream convert(dest_job_str); //Conversion of string to int
		int dest_job;
		convert >> dest_job;
		dest_job -= 1;
		if (dest_job >= job_vector.size() || dest_job < 0) {
			cout << "Process not found!" << endl;
			return;
		}
		pid_t dest_pid = job_vector.at(dest_job).command_vector.at(0).pid;
		kill(dest_pid, SIGCONT);
		job_vector.at(dest_job).Running = true;
		job_vector.at(dest_job).Completed = false;
		waitpid(dest_pid, 0, WUNTRACED); //Original intention here was to wait for child to exit normally, but apparently WIFEXITED was not declared within the scope
		job_vector.erase(job_vector.begin() + job_vector.size() - 1);
		return;
	}
	else if (job_vector.back().command_vector.back().string_command == "bg") {
		string dest_job_str = job_vector.back().command_vector.back().string_inputs.at(0);
		stringstream convert(dest_job_str); //Converts string to int
		int dest_job;
		convert >> dest_job;
		dest_job -= 1;
		if (dest_job >= job_vector.size() || dest_job < 0) {
			cout << "Process not found!" << endl;
			return;
		}
		pid_t dest_pid = job_vector.at(dest_job).command_vector.at(0).pid;
		kill(dest_pid, SIGCONT);
		job_vector.at(dest_job).Running = true;
		job_vector.at(dest_job).Completed = false;
		kill(dest_pid, SIGCONT);
		return;
	}
	else { //Execute commands normally
		if (Exec_Commands(job_vector.back().command_vector)) {
			job_vector.back().Completed = true;
			job_vector.back().Running = false;
		}
	}
}