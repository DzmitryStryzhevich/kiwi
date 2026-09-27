"""Exception hierarchy shared by kiwicgen subsystems and frontends."""


class KiwicgenError(RuntimeError):
    """Base error raised for expected kiwicgen failures."""


class ConfigurationError(KiwicgenError):
    """Raised when generation configuration is invalid or unsupported."""


class ProfileError(KiwicgenError):
    """Raised when a generation profile cannot be consumed safely."""


class ResourceError(KiwicgenError):
    """Raised when a required runtime resource cannot be resolved."""


class TemplateError(KiwicgenError):
    """Raised when a source template cannot be read or transformed."""


class GenerationError(KiwicgenError):
    """Raised when output generation cannot be completed."""


class FormattingError(KiwicgenError):
    """Raised when generated-source formatting cannot be completed."""
