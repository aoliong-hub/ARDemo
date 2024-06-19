import resourceManager from '@ohos.resourceManager';

export const start:(id:string)=>void;
export const show:(id:string)=>void;
export const hide:(id:string)=>void;
export const update:(id:string)=>void;
export const stop:(id:string)=>void;
export const init:(resmgr : resourceManager.ResourceManager)=>void;