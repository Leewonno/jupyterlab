/* 외부 서버와 통신을 위한 중앙 관리 영역 */

import { Token } from '@lumino/coreutils';

// ===============================================
// 파일 제출 여부 확인
// ===============================================

// 파일 열기 전달값
export interface IFileOpenInfo {
  path: string;
  name: string;
  openedAt: string;
}

// ===============================================
// 저장 및 제출 시 서버, 클라에 필요한 데이터 (과목/주차 목록)
// ===============================================

// 주차 항목 (isDeadline 이 true 면 마감되어 선택할 수 없음)
export interface IWeek {
  title: string;
  isDeadline: boolean;
}

// 과목/주차 목록
export interface ISubmitMetadata {
  subjects: string[];
  weeks: IWeek[];
}

// 제출 시 파일과 함께 보내는 메타데이터 (추후 학생 정보(토큰) 등도 함께 전달 필요)
export interface ISubmitMeta {
  subject: string;
  week: string;
}

// 제출 응답
export interface ISubmitResult {
  saved?: unknown[];
}

// ===============================================
// 공통 코드
// ===============================================

// 외부 API 연결을 관리하는 중앙 클라이언트
export interface ICustomApi {
  baseUrl: string; // 외부 서버 base URL
  request<T = unknown>(endpoint: string, init?: RequestInit): Promise<T>; // 외부 서버로 JSON 요청을 보내고 응답을 파싱
  checkSubmitFile(info: IFileOpenInfo): Promise<void>; // 파일 열기 이벤트를 외부 서버로 보고
  getSubmitMetadata(): Promise<ISubmitMetadata>; // 과목/주차 목록을 가져옴

  submitFile(
    filename: string,
    content: string,
    mimeType: string,
    meta: ISubmitMeta
  ): Promise<ISubmitResult>; // 저장된 파일 내용을 메타데이터와 함께 서버로 전송
}

// 중앙 API 클라이언트 토큰
export const ICustomApi = new Token<ICustomApi>(
  '@jupyterlab/custom-api-extension:ICustomApi',
  'Central client for all external API connections.'
);
