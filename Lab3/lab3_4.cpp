#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <Windows.h>

#define DATA_FILE "data.dat"
#define LEN_MAX 20
#define MAX_RAND 255

#define READERS_COUNT 4
#define THREADS_COUNT READERS_COUNT + 1
#define WRITER_IDX 0
#define FIRST_READER_IDX 1
#define WAIT_LIMIT 100

bool FileExists(const char* fileName);
inline void CreateDataFile(const char* fileName);
char* GenerateData();
inline void InitDataFile(const char* fileName);

HANDLE ghWriteEvent;

int activeReadersCount = 0;
bool inWriteMode = false;
UINT32 writerOnWaiting = 0;
bool isWriterHungry = false;

DWORD WINAPI WriteData(LPVOID lpParam);
DWORD WINAPI ReadData(LPVOID lpParam);

int main() {

	//проверяем наличие файла с данными
	if (!FileExists(DATA_FILE)) {
		//создаём файл в случае его отсутствия
		CreateDataFile(DATA_FILE);
	}

	//инициализируем данные и заполняем файл
	InitDataFile(DATA_FILE);

	//объявляем массивы потоков
	HANDLE hThreadArray[THREADS_COUNT];
	DWORD dwThreadIdArray[THREADS_COUNT];

	//выделяем память под рабочие данные потоков
	char** dataArray = (char**)calloc(THREADS_COUNT, sizeof(char*));
	if (dataArray == NULL) {
		return 1;
	}

	for (int idx = 0; idx < THREADS_COUNT; idx++) {
		dataArray[idx] = (char*)calloc(LEN_MAX, sizeof(char));
	}

	//запускаем процесс непрерывной работы с файлом данных
	int processExitCode = 0;
	while (true) {
		if (isWriterHungry && activeReadersCount > 0) {
			//останавливаем все операции чтения, чтобы сделать запись
			for (int idx = FIRST_READER_IDX; idx < THREADS_COUNT; idx++) {
				 TerminateThread(hThreadArray[idx], 0);
			}
		}

		//данные для записи
		dataArray[WRITER_IDX] = GenerateData();

		//инициализируем писателя
		hThreadArray[WRITER_IDX] = CreateThread(
			NULL,
			0,
			WriteData,
			dataArray[WRITER_IDX],
			0,
			&dwThreadIdArray[WRITER_IDX]);

		if (hThreadArray[WRITER_IDX] == NULL) {
			printf("Can't start \"Writer\": %d\n", GetLastError());
			processExitCode = 1;
			break;
		}

		//инициализируем читателей
		for (int idx = FIRST_READER_IDX; idx < THREADS_COUNT; idx++) {
			hThreadArray[idx] = CreateThread(
				NULL,
				0,
				ReadData,
				&dwThreadIdArray[idx],
				0,
				&dwThreadIdArray[idx]);

			if (hThreadArray[idx] == NULL) {
				printf("Can't start \"Readers\": %d\n", GetLastError());
				processExitCode = 1;
				break;
			}
		}
	}

	return processExitCode;
}

#pragma region FileUtils
bool FileExists(const char* fileName) {
	FILE* dataFile = fopen(fileName, "r");
	const bool condition = dataFile != NULL;

	if (condition) {
		fclose(dataFile);
	}

	return condition;
}

inline void CreateDataFile(const char* fileName) {
	FILE* dataFile = fopen(fileName, "w");
	fclose(dataFile);
}

char* GenerateData() {
	srand(time(NULL));

	char* data = (char*)calloc(LEN_MAX, sizeof(char));
	if (data != NULL) {
		for (int idx = 0; idx < LEN_MAX; idx++) {
			data[idx] = (char)(rand() % RAND_MAX);
		}
	}

	return data;
}

inline void InitDataFile(const char* fileName) {
	FILE* dataFile = fopen(fileName, "w");
	char* data = GenerateData();
	if (data != NULL) {
		fprintf(dataFile, data);
	}

	fclose(dataFile);
}
#pragma endregion

DWORD WINAPI WriteData(LPVOID lpParam) {
	if (activeReadersCount > 0 || inWriteMode) {
		writerOnWaiting++;
		if (writerOnWaiting == WAIT_LIMIT) {
			isWriterHungry = true;
		}

		return 0;
	}

	//запускаем событие записи данных
	ghWriteEvent = CreateEvent(
		NULL,
		TRUE,
		FALSE,
		TEXT("Write Data"));

	if (ghWriteEvent == NULL) {
		printf("Can't create event of writing \"%s\": %d\n", DATA_FILE, GetLastError());
		inWriteMode = false;
		return 1;
	}

	inWriteMode = true;
	writerOnWaiting = 0;
	isWriterHungry = false;

	//запись данных
	FILE* dataFile = fopen(DATA_FILE, "a");
	const char* data = (char*)lpParam;
	if (dataFile != NULL) {
		if (data != NULL) {
			printf("Write: %s\n", data);

			fprintf(dataFile, data);
		}

		fclose(dataFile);
	}

	//сигнализируем о завершении события
	if (!SetEvent(ghWriteEvent)) {
		printf("SetEvent failed (%d)\n", GetLastError());
		return 1;
	}

	inWriteMode = false;
}

DWORD WINAPI ReadData(LPVOID lpParam) {
	if (activeReadersCount >= READERS_COUNT || isWriterHungry) {
		return 0;
	}

	DWORD dwWaitResult = WaitForSingleObject(
		ghWriteEvent,
		INFINITE);

	switch (dwWaitResult) {
	case WAIT_OBJECT_0: {
			activeReadersCount++;

			FILE* dataFile = fopen(DATA_FILE, "r");
			char* data = (char*)lpParam;

			if (dataFile != NULL) {
				if (data != NULL) {
					fscanf(dataFile, "%s", data);
				}

				printf("Read: %s\n", data);

				fclose(dataFile);
				activeReadersCount--;
			}
		}

		break;

	default: {
			printf("Error while waiting for writing data: (%d)\n", GetLastError());
		}

		return 1;
	}
}
