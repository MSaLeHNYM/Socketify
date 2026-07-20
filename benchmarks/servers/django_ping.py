"""Minimal Django ASGI/WSGI-less: use Django's runserver alternative via WSGI + waitress."""
import os
import sys

PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 19083
os.environ.setdefault("DJANGO_SETTINGS_MODULE", "django_ping_settings")

# Keep settings next to this file
sys.path.insert(0, os.path.dirname(__file__))

from django.conf import settings

if not settings.configured:
    settings.configure(
        DEBUG=False,
        ROOT_URLCONF=__name__,
        SECRET_KEY="bench-only-not-secret",
        ALLOWED_HOSTS=["*"],
        MIDDLEWARE=[],  # strip middleware for a fair micro-benchmark
        INSTALLED_APPS=[],
    )

from django.http import JsonResponse
from django.urls import path

def ping(_request):
    return JsonResponse({"ok": True})

urlpatterns = [path("ping", ping)]

if __name__ == "__main__":
    import django
    django.setup()
    from django.core.wsgi import get_wsgi_application
    from waitress import serve
    serve(get_wsgi_application(), host="127.0.0.1", port=PORT, threads=32, channel_timeout=120)
