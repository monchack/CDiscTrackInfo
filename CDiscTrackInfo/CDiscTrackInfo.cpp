// CDiscTrackInfo.cpp : このファイルには 'main' 関数が含まれています。プログラム実行の開始と終了がそこで行われます。
//

#include "pch.h"
#include <iostream>

#include <windows.h>
#include <winioctl.h>
#include <ntddcdrm.h>
#include <ntddscsi.h>
#include <stddef.h>


// CDのドライブレター取得
HANDLE open_first_cd_drive()
{
	HANDLE fh = 0;

	const wchar_t* drives[] = {
		L"\\\\.\\A:", L"\\\\.\\B:", L"\\\\.\\C:", L"\\\\.\\D:", L"\\\\.\\E:", L"\\\\.\\F:",
		L"\\\\.\\G:", L"\\\\.\\H:", L"\\\\.\\I:", L"\\\\.\\J:", L"\\\\.\\K:", L"\\\\.\\L:",
		L"\\\\.\\M:", L"\\\\.\\N:", L"\\\\.\\O:", L"\\\\.\\P:", L"\\\\.\\Q:", L"\\\\.\\R:",
		L"\\\\.\\S:", L"\\\\.\\T:", L"\\\\.\\U:", L"\\\\.\\V:", L"\\\\.\\W:", L"\\\\.\\X:",
		L"\\\\.\\Y:", L"\\\\.\\Z:",
	};

	DWORD drive = GetLogicalDrives();
	for (int i = 0, flag = 1; i < 26; i++, flag <<= 1) {
		if (!(drive & flag)) continue;
		char letter[20];
		sprintf_s(letter, sizeof(letter), "%c:\\", 'A' + i);
		if (GetDriveTypeA(letter) == DRIVE_CDROM)
		{
			//見つかった
			fh = CreateFileW(drives[i], GENERIC_READ | GENERIC_WRITE,
				FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
				FILE_ATTRIBUTE_NORMAL, NULL);
			return fh;
		}
	}
	return 0;
}


// 各トラックの開始位置を 秒 で格納する
int get_pos_min(HANDLE fh, unsigned int* pos)
{
	DWORD ioctl_bytes;
	BOOL ioctl_rv;

	// READ TOC (MSF) MSF: Minute, Second, Frame
	const unsigned char cdb[] = { 0x43, 2, 0x00, 0, 0, 0, 0, 4, 0, 0x00, 0, 0 };

	UCHAR buf[2352];
	struct sptd_with_sense
	{
		SCSI_PASS_THROUGH_DIRECT s;
		UCHAR sense[128];
	} sptd;

	memset(&sptd, 0, sizeof(sptd));
	sptd.s.Length = sizeof(sptd.s);
	sptd.s.CdbLength = sizeof(cdb);
	sptd.s.DataIn = SCSI_IOCTL_DATA_IN;
	sptd.s.TimeOutValue = 30;
	sptd.s.DataBuffer = buf;
	sptd.s.DataTransferLength = sizeof(buf);
	sptd.s.SenseInfoLength = sizeof(sptd.sense);
	sptd.s.SenseInfoOffset = offsetof(struct sptd_with_sense, sense);
	memcpy(sptd.s.Cdb, cdb, sizeof(cdb));

	ioctl_rv = DeviceIoControl(fh, IOCTL_SCSI_PASS_THROUGH_DIRECT, &sptd,
		sizeof(sptd), &sptd, sizeof(sptd), &ioctl_bytes, NULL);

	// buf[0]  0x11 -> pre emphasis      0x01 -> no emphasis

	if (sptd.sense[0] != 0)
	{
		return 0;
	}

	int i = 0;
	for (i = 0; i <= buf[3]; ++i)
	{
		
		unsigned int a =
			(unsigned int)buf[8 + i * 8 + 1] * 60 +
			(unsigned int)buf[8 + i * 8 + 2];
		pos[i] = a;
	}

	int j = 0;
	for (j = 0; j < i; ++j)
	{
		pos[j] = pos[j + 1] - pos[j];
	}
	return j;
}

int get_pos(HANDLE fh, unsigned int* pos, int* pre_emphasis)
{
	DWORD ioctl_bytes = 0;
	BOOL ioctl_rv;

	// READ TOC (LBA)
	const unsigned char cdb[] = { 0x43, 0, 0x00, 0, 0, 0, 0, 1, 0, 0x00, 0, 0 };

	UCHAR buf[2352];
	struct sptd_with_sense
	{
		SCSI_PASS_THROUGH_DIRECT s;
		UCHAR sense[128];
	} sptd;

	memset(&sptd, 0, sizeof(sptd));
	sptd.s.Length = sizeof(sptd.s);
	sptd.s.CdbLength = sizeof(cdb);
	sptd.s.DataIn = SCSI_IOCTL_DATA_IN;
	sptd.s.TimeOutValue = 30;
	sptd.s.DataBuffer = buf;
	sptd.s.DataTransferLength = sizeof(buf);
	sptd.s.SenseInfoLength = sizeof(sptd.sense);
	sptd.s.SenseInfoOffset = offsetof(struct sptd_with_sense, sense);
	memcpy(sptd.s.Cdb, cdb, sizeof(cdb));

	ioctl_rv = DeviceIoControl(fh, IOCTL_SCSI_PASS_THROUGH_DIRECT, &sptd,
		sizeof(sptd), &sptd, sizeof(sptd), &ioctl_bytes, NULL);

	// buf[0]  0x11 -> pre emphasis      0x01 -> no emphasis

	if (sptd.sense[0] != 0) // 70h (current error), 71h(deferred error), 72h, 7Eh(vendor error)    00h to 6Fh are not defined
	{
		return 0;
	}

	int i = 0;
	for (i = 0; i < buf[3]; ++i)
	{
		unsigned int a =
			(unsigned int)buf[8 + i * 8] << 24 |
			(unsigned int)buf[8 + i * 8 +1] << 16 |
			(unsigned int)buf[8 + i * 8 + 2] << 8 |
			(unsigned int)buf[8 + i * 8 + 3];
		pos[i] = a;

		if (buf[5 + i * 8] & 0x01) pre_emphasis[i] = 1;
		else pre_emphasis[i] = 0;
	}
	return i;
}


int check(HANDLE fh, unsigned int pos)
{
	DWORD ioctl_bytes;
	BOOL ioctl_rv;

	// READ CD (0xBE)  第1セクタから1セクタ読み出す　010 = Qサブチャネルデータ 
	unsigned char cdb[] = { 0xBE, 0, 0, 0, 0, 0/*pos*/, 0, 0, 1, 0x00, 2, 0 }; // 動作確認済み
	unsigned char* p = (unsigned char*)&pos;
	cdb[2] = p[3];
	cdb[3] = p[2];
	cdb[4] = p[1];
	cdb[5] = p[0];

	UCHAR buf[2352];
	struct sptd_with_sense
	{
		SCSI_PASS_THROUGH_DIRECT s;
		UCHAR sense[128];
	} sptd;

	memset(&sptd, 0, sizeof(sptd));
	sptd.s.Length = sizeof(sptd.s);
	sptd.s.CdbLength = sizeof(cdb);
	sptd.s.DataIn = SCSI_IOCTL_DATA_IN;
	sptd.s.TimeOutValue = 30;
	sptd.s.DataBuffer = buf;
	sptd.s.DataTransferLength = sizeof(buf);
	sptd.s.SenseInfoLength = sizeof(sptd.sense);
	sptd.s.SenseInfoOffset = offsetof(struct sptd_with_sense, sense);
	memcpy(sptd.s.Cdb, cdb, sizeof(cdb));

	ioctl_rv = DeviceIoControl(fh, IOCTL_SCSI_PASS_THROUGH_DIRECT, &sptd,
		sizeof(sptd), &sptd, sizeof(sptd), &ioctl_bytes, NULL);

	// buf[0]  0x11 -> pre emphasis      0x01 -> no emphasis

	if (buf[0] & 0x10) return 1;

	return 0;
}



int main()
{
	unsigned int pos[128];
	unsigned int pos_min[128];
	int pre_emphasis_toc[128];

	HANDLE fh;
	fh = open_first_cd_drive();
	if (fh == 0)
	{
		std::cout << "No CD drive was found.\n";
		return 0;
	}

	int num = 	get_pos(fh, pos, pre_emphasis_toc);
	if (num == 0)
	{
		std::cout << "No Compact Disc was found.\n";
	}

	get_pos_min(fh, pos_min);
	for (int i = 0; i < num; ++i)
	{
		if (check(fh, pos[i])) std::cout << "track " << i+1 << "  " << pos_min[i] << " sec   pre-emphasis: yes" << "  (toc pre_emphasis: " << pre_emphasis_toc[i] << ")";
		else                   std::cout << "track " << i+1 << "  " << pos_min[i] << " sec   pre-emphasis: no " << "  (toc pre_emphasis: " << pre_emphasis_toc[i] << ")";
		std::cout << "\n"; 
	}

	CloseHandle(fh);

}

// プログラムの実行: Ctrl + F5 または [デバッグ] > [デバッグなしで開始] メニュー
// プログラムのデバッグ: F5 または [デバッグ] > [デバッグの開始] メニュー

// 作業を開始するためのヒント: 
//    1. ソリューション エクスプローラー ウィンドウを使用してファイルを追加/管理します 
//   2. チーム エクスプローラー ウィンドウを使用してソース管理に接続します
//   3. 出力ウィンドウを使用して、ビルド出力とその他のメッセージを表示します
//   4. エラー一覧ウィンドウを使用してエラーを表示します
//   5. [プロジェクト] > [新しい項目の追加] と移動して新しいコード ファイルを作成するか、[プロジェクト] > [既存の項目の追加] と移動して既存のコード ファイルをプロジェクトに追加します
//   6. 後ほどこのプロジェクトを再び開く場合、[ファイル] > [開く] > [プロジェクト] と移動して .sln ファイルを選択します
