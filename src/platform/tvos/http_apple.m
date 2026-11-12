/* SPDX-License-Identifier: MIT
 * tvOS HTTPS through Foundation. The delegate bounds downloads before buffering them.
 * No delegate holds the caller's output buffer after this function returns. */
#import <Foundation/Foundation.h>
#include <string.h>

@interface OFPHTTPReader : NSObject <NSURLSessionDataDelegate>
@property(nonatomic) NSMutableData *data;
@property(nonatomic) NSUInteger capacity;
@property(nonatomic) NSInteger status;
@property(nonatomic) dispatch_semaphore_t finished;
@end
@implementation OFPHTTPReader
- (void)URLSession:(NSURLSession *)session dataTask:(NSURLSessionDataTask *)task
 didReceiveResponse:(NSURLResponse *)response completionHandler:(void (^)(NSURLSessionResponseDisposition))finish
{
    (void)session; (void)task;
    self.status = [(NSHTTPURLResponse *)response statusCode];
    if (response.expectedContentLength >= 0 && (unsigned long long)response.expectedContentLength >= self.capacity) {
        self.status = -1; finish(NSURLSessionResponseCancel);
    } else finish(NSURLSessionResponseAllow);
}
- (void)URLSession:(NSURLSession *)session dataTask:(NSURLSessionDataTask *)task didReceiveData:(NSData *)data
{
    (void)session;
    if (data.length >= self.capacity - self.data.length) { self.status = -1; [task cancel]; }
    else [self.data appendData:data];
}
- (void)URLSession:(NSURLSession *)session task:(NSURLSessionTask *)task didCompleteWithError:(NSError *)error
{
    (void)session; (void)task;
    if (error) self.status = -1;
    dispatch_semaphore_signal(self.finished);
}
@end

int ofp_apple_http(const char *url, const char *body, const char *type, char *out, size_t cap,
                   int timeout_ms, void (*idle)(void *), void *ctx)
{
    @autoreleasepool {
        if (!url || !out || !cap || timeout_ms <= 0) return -1;
        out[0] = 0;
        NSURL *address = [NSURL URLWithString:@(url)];
        if (!address || (![[address scheme] isEqualToString:@"https"] && ![[address scheme] isEqualToString:@"http"])) return -1;
        OFPHTTPReader *reader = [OFPHTTPReader new];
        reader.capacity = cap; reader.status = -1; reader.data = [NSMutableData data];
        reader.finished = dispatch_semaphore_create(0);
        NSURLSessionConfiguration *config = [NSURLSessionConfiguration ephemeralSessionConfiguration];
        config.timeoutIntervalForRequest = config.timeoutIntervalForResource = timeout_ms / 1000.0;
        NSOperationQueue *queue = [NSOperationQueue new]; queue.maxConcurrentOperationCount = 1;
        NSURLSession *session = [NSURLSession sessionWithConfiguration:config delegate:reader delegateQueue:queue];
        NSMutableURLRequest *request = [NSMutableURLRequest requestWithURL:address];
        [request setValue:@"OpenFirePower/0.1" forHTTPHeaderField:@"User-Agent"];
        if (body) {
            request.HTTPMethod = @"POST"; request.HTTPBody = [NSData dataWithBytes:body length:strlen(body)];
            [request setValue:@(type) forHTTPHeaderField:@"Content-Type"];
        }
        [[session dataTaskWithRequest:request] resume];
        while (dispatch_semaphore_wait(reader.finished, dispatch_time(DISPATCH_TIME_NOW, 10000000)) != 0)
            if (idle) idle(ctx);
        int result = (int)reader.status;
        if (result > 0) { memcpy(out, reader.data.bytes, reader.data.length); out[reader.data.length] = 0; }
        [session finishTasksAndInvalidate];
        return result;
    }
}
