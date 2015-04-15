//! HttpError and HttpResult module.
use std::error::Error;
use std::fmt;
use std::io::Error as IoError;

use httparse;
use url;

use self::HttpError::{HttpMethodError, HttpUriError, HttpVersionError,
                      HttpHeaderError, HttpStatusError, HttpIoError,
                      HttpTooLargeError, HttpClosed};


/// Result type often returned from methods that can have `HttpError`s.
pub type HttpResult<T> = Result<T, HttpError>;

/// A set of errors that can occur parsing HTTP streams.
#[derive(Debug)]
pub enum HttpError {
    /// An invalid `Method`, such as `GE,T`.
    HttpMethodError,
    /// An invalid `RequestUri`, such as `exam ple.domain`.
    HttpUriError(url::ParseError),
    /// An invalid `HttpVersion`, such as `HTP/1.1`
    HttpVersionError,
    /// An invalid `Header`.
    HttpHeaderError,
    /// A message head is too large to be reasonable.
    HttpTooLargeError,
    /// An invalid `Status`, such as `1337 ELITE`.
    HttpStatusError,
    /// An `IoError` that occured while trying to read or write to a network stream.
    HttpIoError(IoError),
    /// TCP FIN
    HttpClosed,
}

impl fmt::Display for HttpError {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        f.write_str(self.description())
    }
}

impl Error for HttpError {
    fn description(&self) -> &str {
        match *self {
            HttpMethodError => "Invalid Method specified",
            HttpUriError(_) => "Invalid Request URI specified",
            HttpVersionError => "Invalid HTTP version specified",
            HttpHeaderError => "Invalid Header provided",
            HttpTooLargeError => "Message head is too large",
            HttpStatusError => "Invalid Status provided",
            HttpIoError(_) => "An IoError occurred while connecting to the specified network",
            HttpClosed => "TCP connection closed",
        }
    }

    fn cause(&self) -> Option<&Error> {
        match *self {
            HttpIoError(ref error) => Some(error),
            HttpUriError(ref error) => Some(error),
            _ => None,
        }
    }
}

impl From<IoError> for HttpError {
    fn from(err: IoError) -> HttpError {
        HttpIoError(err)
    }
}

impl From<url::ParseError> for HttpError {
    fn from(err: url::ParseError) -> HttpError {
        HttpUriError(err)
    }
}

impl From<httparse::Error> for HttpError {
    fn from(err: httparse::Error) -> HttpError {
        match err {
            httparse::Error::HeaderName => HttpHeaderError,
            httparse::Error::HeaderValue => HttpHeaderError,
            httparse::Error::NewLine => HttpHeaderError,
            httparse::Error::Status => HttpStatusError,
            httparse::Error::Token => HttpHeaderError,
            httparse::Error::TooManyHeaders => HttpTooLargeError,
            httparse::Error::Version => HttpVersionError,
        }
    }
}
