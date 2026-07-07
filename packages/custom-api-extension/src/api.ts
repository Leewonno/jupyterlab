/* 외부 서버와 통신을 위한 중앙 관리 영역 */

import type {
  ICustomApi,
  IFileOpenInfo,
  ISubmitMeta,
  ISubmitMetadata,
  ISubmitResult
} from './tokens';

// base URL
const API_BASE_URL = 'http://127.0.0.1:9000';

// 중앙 API 클래스
export class CustomApi implements ICustomApi {
  constructor(baseUrl: string = API_BASE_URL) {
    this._baseUrl = baseUrl.replace(/\/+$/, ''); // 끝의 슬래시 제거 -> endpoint 결합 시 '//' 중복 제거
  }

  get baseUrl(): string {
    return this._baseUrl;
  }

  async request<T = unknown>(
    endpoint: string,
    init: RequestInit = {}
  ): Promise<T> {
    const url = `${this._baseUrl}/${endpoint.replace(/^\/+/, '')}`;
    const isFormData =
      typeof FormData !== 'undefined' && init.body instanceof FormData;
    const response = await fetch(url, {
      // 외부 서버이므로 Jupyter의 자격증명 보내지 않음 ('omit')
      credentials: 'omit',
      ...init,
      headers: {
        // GET 요청에서 CORS preflight 방지를 위해 JSON body 존재 시에만 content-type 붙임
        // FormData 일 때는 Content-Type 건드리지 않음
        ...(init.body != null && !isFormData
          ? { 'Content-Type': 'application/json' }
          : {}),
        ...(init.headers ?? {})
      }
    });
    if (!response.ok) {
      throw new Error(
        `API request failed: ${response.status} ${response.statusText} (${url})`
      );
    }
    const text = await response.text();
    return (text ? JSON.parse(text) : undefined) as T;
  }

  // ===============================================
  // 파일 제출 확인
  // ===============================================
  checkSubmitFile(info: IFileOpenInfo): Promise<void> {
    // 파일명을 ?filename= 쿼리스트링으로 붙여 GET 으로 보냄
    const query = new URLSearchParams({ filename: info.name }).toString();
    return this.request<void>(`file-open?${query}`, { method: 'GET' });
  }

  // ===============================================
  // 저장 및 제출 팝업에 필요한 데이터 GET
  // ===============================================
  async getSubmitMetadata(): Promise<ISubmitMetadata> {
    const data = await this.request<Partial<ISubmitMetadata>>('meta/', {
      method: 'GET'
    });
    // 서버 응답 누락 필드를 안전한 기본값으로 채움
    return {
      subjects: data?.subjects ?? [],
      weeks: (data?.weeks ?? []).map(week => ({
        title: week.title,
        isDeadline: Boolean(week.isDeadline)
      }))
    };
  }

  // ===============================================
  // 저장 및 제출 POST
  // ===============================================
  submitFile(
    filename: string,
    content: string,
    mimeType: string,
    meta: ISubmitMeta
  ): Promise<ISubmitResult> {
    const formData = new FormData();
    formData.append('files', new Blob([content], { type: mimeType }), filename);
    formData.append('subject', meta.subject);
    formData.append('week', meta.week);

    return this.request<ISubmitResult>('file/', {
      method: 'POST',
      body: formData
    });
  }

  private readonly _baseUrl: string;
}
